#include <cstdlib>
#include <dpp/dpp.h>
#include <iostream>
#include <string>
#include <map>
#include <sstream>

// Global map to store voice clients for each guild
std::map<dpp::snowflake, dpp::discord_voice_client*> voice_clients;

int main() {
    // === STEP 1: GET BOT TOKEN ===
    char* buffer = nullptr;
    size_t size = 0;
    if (_dupenv_s(&buffer, &size, "DISCORD_BOT_TOKEN") != 0 || buffer == nullptr) {
        std::cerr << "Error: environment variable not set.\n";
        return 1;
    }
    std::string token(buffer);
    free(buffer);

    // === STEP 2: CREATE BOT ===
    dpp::cluster bot(token, dpp::i_default_intents | dpp::i_message_content | dpp::i_guild_voice_states);
    bot.on_log(dpp::utility::cout_logger());

    // === STEP 3: REGISTER COMMANDS ===
    bot.on_ready([&bot](const dpp::ready_t& event) {
        std::cout << "Bot is online!\n";
        if (dpp::run_once<struct register_bot_commands>()) {

            // OPTION 1: Register globally (takes up to 1 hour)
            // Uncomment these if you want global commands:

            bot.global_command_create(dpp::slashcommand("join", "Join your voice channel", bot.me.id));
            dpp::slashcommand play_cmd("play", "Play music from YouTube", bot.me.id);
            play_cmd.add_option(dpp::command_option(dpp::co_string, "url", "YouTube URL", true));
            bot.global_command_create(play_cmd);
            bot.global_command_create(dpp::slashcommand("stop", "Stop playing music", bot.me.id));
            bot.global_command_create(dpp::slashcommand("leave", "Leave voice channel", bot.me.id));


            // OPTION 2: Register for specific guild (INSTANT!)
            // Replace YOUR_GUILD_ID with your server's ID
            //dpp::snowflake guild_id = 0; // <-- PUT YOUR SERVER ID HERE

            //if (guild_id != 0) {
            //    // /join command
            //    bot.guild_command_create(dpp::slashcommand("join", "Join your voice channel", bot.me.id), guild_id);

            //    // /play command with URL parameter
            //    dpp::slashcommand play_cmd("play", "Play music from YouTube", bot.me.id);
            //    play_cmd.add_option(dpp::command_option(dpp::co_string, "url", "YouTube URL", true));
            //    bot.guild_command_create(play_cmd, guild_id);

            //    // /stop command
            //    bot.guild_command_create(dpp::slashcommand("stop", "Stop playing music", bot.me.id), guild_id);

            //    // /leave command
            //    bot.guild_command_create(dpp::slashcommand("leave", "Leave voice channel", bot.me.id), guild_id);

            //    std::cout << "Guild commands registered instantly!\n";
            //}
            //else {
            //    std::cout << "WARNING: Set your guild_id in the code to register commands instantly!\n";
            //}
        }
        });

    // === STEP 4: HANDLE COMMANDS ===
    bot.on_slashcommand([&bot](const dpp::slashcommand_t& event) {

        // === /JOIN COMMAND ===
        if (event.command.get_command_name() == "join") {
            dpp::guild* g = dpp::find_guild(event.command.guild_id);
            if (!g) {
                event.reply("Guild not found!");
                return;
            }
            if (!g->connect_member_voice(bot, event.command.get_issuing_user().id)) {
                event.reply("You're not in a voice channel!");
                return;
            }
            event.reply("Joining your voice channel!");
        }

        // === /PLAY COMMAND ===
        else if (event.command.get_command_name() == "play") {
            // Get the URL parameter
            std::string url = std::get<std::string>(event.get_parameter("url"));

            // Check if we have a voice client for this guild
            auto vc_it = voice_clients.find(event.command.guild_id);
            if (vc_it == voice_clients.end() || !vc_it->second) {
                event.reply("I'm not in a voice channel! Use /join first.");
                return;
            }

            dpp::discord_voice_client* voice_client = vc_it->second;

            if (!voice_client->is_ready()) {
                event.reply("Voice connection not ready. Please wait a moment.");
                return;
            }

            // Reply immediately so Discord doesn't timeout
            event.reply("Downloading and playing: " + url);

            // === STEP A: Use yt-dlp to get audio stream URL ===
            std::string yt_dlp_command = "yt-dlp -f bestaudio -g \"" + url + "\"";

            // Run yt-dlp and capture output
            FILE* pipe = _popen(yt_dlp_command.c_str(), "r");
            if (!pipe) {
                std::cerr << "Failed to run yt-dlp!\n";
                return;
            }

            char buffer_out[512];
            std::string audio_url;
            if (fgets(buffer_out, sizeof(buffer_out), pipe) != nullptr) {
                audio_url = buffer_out;
                // Remove newline at the end
                audio_url.erase(audio_url.find_last_not_of("\n\r") + 1);
            }
            _pclose(pipe);

            if (audio_url.empty()) {
                std::cerr << "Failed to get audio URL from yt-dlp!\n";
                return;
            }

            std::cout << "Got audio URL: " << audio_url << std::endl;

            // === STEP B: Stream audio using FFmpeg ===
            // Build FFmpeg command:
            // -i = input (audio URL from yt-dlp)
            // -f s16le = output format (signed 16-bit little-endian PCM)
            // -ar 48000 = sample rate (48kHz, required by Discord)
            // -ac 2 = audio channels (2 = stereo)
            // -loglevel quiet = suppress FFmpeg output
            // pipe:1 = output to stdout (so we can read it)

            std::string ffmpeg_command = "ffmpeg -i \"" + audio_url +
                "\" -f s16le -ar 48000 -ac 2 -loglevel quiet pipe:1";

            std::cout << "Starting FFmpeg stream..." << std::endl;

            // Open FFmpeg process
            FILE* ffmpeg_pipe = _popen(ffmpeg_command.c_str(), "rb");
            if (!ffmpeg_pipe) {
                std::cerr << "Failed to start FFmpeg!\n";
                return;
            }

            // === STEP C: Read from FFmpeg and send to Discord ===
            // Discord expects audio in chunks
            const size_t chunk_size = 11520; // 60ms of 48kHz stereo 16-bit audio
            int16_t pcm_buffer[chunk_size];

            std::cout << "Streaming audio to Discord..." << std::endl;

            while (true) {
                // Read audio data from FFmpeg
                size_t bytes_read = fread(pcm_buffer, sizeof(int16_t), chunk_size, ffmpeg_pipe);

                if (bytes_read == 0) {
                    // End of audio
                    break;
                }

                // Send audio to Discord
                // send_audio_raw sends PCM audio data
                voice_client->send_audio_raw((uint16_t*)pcm_buffer, bytes_read);
            }

            _pclose(ffmpeg_pipe);
            std::cout << "Finished playing audio!" << std::endl;
        }

        // === /STOP COMMAND ===
        else if (event.command.get_command_name() == "stop") {
            auto vc_it = voice_clients.find(event.command.guild_id);
            if (vc_it == voice_clients.end() || !vc_it->second) {
                event.reply("Not playing anything!");
                return;
            }

            vc_it->second->stop_audio();
            event.reply("Stopped playing!");
        }

        // === /LEAVE COMMAND ===
        else if (event.command.get_command_name() == "leave") {
            auto vc_it = voice_clients.find(event.command.guild_id);
            if (vc_it == voice_clients.end()) {
                event.reply("I'm not in a voice channel!");
                return;
            }

            // Remove from our tracking map
            voice_clients.erase(event.command.guild_id);

            // Get the guild and disconnect
            dpp::guild* g = dpp::find_guild(event.command.guild_id);
            if (g) {
                // The bot will automatically disconnect when we stop tracking it
                event.reply("Left voice channel!");
            }
        }
        });

    // === STEP 5: VOICE READY EVENT ===
    // When bot successfully joins voice, store the voice client
    bot.on_voice_ready([](const dpp::voice_ready_t& event) {
        std::cout << "Voice ready in guild " << event.voice_client->server_id << std::endl;

        // Store the voice client so we can use it in /play command
        voice_clients[event.voice_client->server_id] = event.voice_client;
        });

    // === STEP 6: VOICE CLIENT DISCONNECT ===
    bot.on_voice_client_disconnect([](const dpp::voice_client_disconnect_t& event) {
        std::cout << "Voice disconnected from guild " << event.voice_client->server_id << std::endl;

        // Remove from our map
        voice_clients.erase(event.voice_client->server_id);
        });

    // === STEP 7: START BOT ===
    bot.start(dpp::st_wait);

    return 0;
}

/*
=== HOW THIS WORKS ===

1. **yt-dlp**: Extracts the direct audio stream URL from YouTube
   - Command: yt-dlp -f bestaudio -g "youtube_url"
   - Returns the actual audio file URL

2. **FFmpeg**: Converts the audio to Discord's required format
   - Format: 16-bit PCM (Pulse Code Modulation)
   - Sample rate: 48,000 Hz (48 kHz)
   - Channels: 2 (stereo)
   - We read this data in chunks

3. **send_audio_raw()**: Sends the audio data to Discord
   - Takes raw PCM audio samples
   - Discord handles the Opus encoding

=== REQUIREMENTS ===

Make sure you have:
✅ yt-dlp.exe in your PATH or same folder
✅ ffmpeg.exe in your PATH or same folder

Test with: yt-dlp --version
Test with: ffmpeg -version

=== TO TEST ===

1. Run the bot
2. Join a voice channel
3. /join
4. /play url:https://www.youtube.com/watch?v=dQw4w9WgXcQ
5. Bot should play the audio!
6. /stop to stop
7. /leave to disconnect

=== LIMITATIONS OF THIS VERSION ===

- Blocks while playing (can't queue songs)
- No error handling for network issues
- No volume control
- No seeking/pausing

We can add these features next if you want!
*/