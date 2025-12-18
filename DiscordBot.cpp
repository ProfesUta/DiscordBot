#include <cstdlib>
#include <dpp/dpp.h>
#include <iostream>
#include <string>
#include <map>
#include <vector>


std::map<dpp::snowflake, dpp::discord_voice_client*> voice_clients;

int main() {
    // === GET BOT TOKEN ===
    char* buffer = nullptr;
    size_t size = 0;
    if (_dupenv_s(&buffer, &size, "DISCORD_BOT_TOKEN") != 0 || buffer == nullptr) {
        std::cerr << "Error: DISCORD_BOT_TOKEN environment variable not set.\n";
        return 1;
    }
    std::string token(buffer);
    free(buffer);

    // === CREATE BOT ===
    dpp::cluster bot(token, dpp::i_default_intents | dpp::i_message_content | dpp::i_guild_voice_states);
    bot.on_log(dpp::utility::cout_logger());

    // === REGISTER COMMANDS ===
    bot.on_ready([&bot](const dpp::ready_t& event) {
        std::cout << "Bot is online!\n";
        if (dpp::run_once<struct register_bot_commands>()) {
            bot.global_command_create(dpp::slashcommand("join", "Join your voice channel", bot.me.id));

            std::cout << "Commands registered!\n";
        }
        });

    // === HANDLE COMMANDS ===
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

        });

    // === START BOT ===
    bot.start(dpp::st_wait);

    return 0;
}