#include <cstdlib>
#include <dpp/dpp.h>
#include <iostream>
#include <string>
#include <map>
#include <thread>
#include <chrono>

// Global map to store voice clients for each guild
std::map<dpp::snowflake, dpp::discord_voice_client*> voice_clients;

int main() {
    // === STEP 1: GET BOT TOKEN ===
    char* buffer = nullptr;
    size_t size = 0;
    if (_dupenv_s(&buffer, &size, "DISCORD_BOT_TOKEN") != 0 || buffer == nullptr) {
        std::cerr << "Error: DISCORD_BOT_TOKEN environment variable not set.\n";
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
            bot.global_command_create(dpp::slashcommand("join", "Join your voice channel", bot.me.id));

            dpp::slashcommand play_cmd("play", "Play music from YouTube", bot.me.id);
            play_cmd.add_option(dpp::command_option(dpp::co_string, "url", "YouTube URL", true));
            bot.global_command_create(play_cmd);

            bot.global_command_create(dpp::slashcommand("stop", "Stop playing music", bot.me.id));
            bot.global_command_create(dpp::slashcommand("leave", "Leave voice channel", bot.me.id));

            std::cout << "Commands registered!\n";
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
            std::string url = std::get<std::string>(event.get_parameter("url"));

            auto vc_it = voice_clients.find(event.command.guild_id);
            if (vc_it == voice_clients.end() || !vc_it->second) {
                event.reply("I'm not in a voice channel! Use /join first.");
                return;
            }

            dpp::discord_voice_client* voice_client = vc_it->second;

            if (!voice_client->is_ready()) {
                event.reply("Voice not ready. Wait and try again.");
                return;
            }

            event.reply("Downloading and playing...");

            std::cout << "\n=== Playing: " << url << " ===" << std::endl;

            // Clean up any old temp files first
            remove("temp_audio.webm");

            // Download with yt-dlp - use web client (most compatible)
            std::string download_cmd = "yt-dlp -f \"ba/b\" -o \"temp_audio.webm\" \"" + url + "\" --quiet --no-warnings";

            std::cout << "Downloading..." << std::endl;
            int dl_result = system(download_cmd.c_str());

            if (dl_result != 0) {
                std::cerr << "Download failed!" << std::endl;
                return;
            }

            std::cout << "Downloaded!" << std::endl;

            // Check the audio format with ffprobe
            std::cout << "Checking audio format..." << std::endl;
            system("ffprobe -v error -show_entries stream=codec_name,sample_rate,channels -of default=noprint_wrappers=1 temp_audio.webm");

            // Stream with FFmpeg - remove -re flag, we'll handle timing ourselves
            std::string ffmpeg_cmd = "ffmpeg -y -i \"temp_audio.webm\" -f s16le -ar 48000 -ac 2 -loglevel quiet pipe:1";

            std::cout << "Playing..." << std::endl;

            FILE* ffmpeg_pipe = _popen(ffmpeg_cmd.c_str(), "rb");
            if (!ffmpeg_pipe) {
                std::cerr << "FFmpeg failed!" << std::endl;
                remove("temp_audio.webm");
                return;
            }

            // Send audio to Discord
            // Use larger buffer - send multiple frames at once for smoother playback
            const size_t samples_per_frame = 960 * 2; // 960 samples per channel * 2 channels
            const size_t frames_per_send = 5; // Send 5 frames (100ms) at once                                        // davigaleeeee
            const size_t buffer_size = samples_per_frame * frames_per_send;

            int16_t pcm_buffer[buffer_size];

            size_t total_frames = 0;

            // Pre-buffer: send first batch immediately
            size_t initial_read = fread(pcm_buffer, sizeof(int16_t), buffer_size, ffmpeg_pipe);
            if (initial_read > 0) {
                voice_client->send_audio_raw(
                    reinterpret_cast<uint16_t*>(pcm_buffer),
                    initial_read
                );
                total_frames += initial_read / samples_per_frame;
            }

            // Now stream with timing
            while (true) {
                size_t read = fread(pcm_buffer, sizeof(int16_t), buffer_size, ffmpeg_pipe);

                if (read == 0) break;

                if (!voice_client->is_ready()) break;

                // Send batch to Discord
                voice_client->send_audio_raw(
                    reinterpret_cast<uint16_t*>(pcm_buffer),
                    read
                );

                total_frames += read / samples_per_frame;

                // Progress indicator
                if (total_frames % 500 == 0) {
                    std::cout << (total_frames * 20 / 1000) << "s" << std::endl;
                }

                // Sleep for the duration of the audio we just sent (100ms for 5 frames)
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }

            _pclose(ffmpeg_pipe);
            remove("temp_audio.webm");

            std::cout << "Finished! (" << (total_frames * 20 / 1000) << " seconds)" << std::endl;
        }

        // === /STOP COMMAND ===
        else if (event.command.get_command_name() == "stop") {
            auto vc_it = voice_clients.find(event.command.guild_id);
            if (vc_it == voice_clients.end() || !vc_it->second) {
                event.reply("Not playing!");
                return;
            }

            vc_it->second->stop_audio();
            event.reply("Stopped!");
        }

        // === /LEAVE COMMAND ===
        else if (event.command.get_command_name() == "leave") {
            auto vc_it = voice_clients.find(event.command.guild_id);
            if (vc_it == voice_clients.end()) {
                event.reply("Not in voice!");
                return;
            }

            voice_clients.erase(event.command.guild_id);
            event.reply("Left!");
        }
        });

    // === STEP 5: VOICE READY EVENT ===
    bot.on_voice_ready([](const dpp::voice_ready_t& event) {
        std::cout << "Voice ready!" << std::endl;
        voice_clients[event.voice_client->server_id] = event.voice_client;
        });

    // === STEP 6: VOICE CLIENT DISCONNECT ===
    bot.on_voice_client_disconnect([](const dpp::voice_client_disconnect_t& event) {
        std::cout << "Voice disconnected" << std::endl;
        voice_clients.erase(event.voice_client->server_id);
        });

    // === STEP 7: START BOT ===
    bot.start(dpp::st_wait);

    return 0;
}