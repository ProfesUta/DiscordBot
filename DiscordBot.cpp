#include <cstdlib>
#include <dpp/dpp.h>
#include <iostream>


int main() {    

    char* buffer = nullptr;
    size_t size = 0;

    if (_dupenv_s(&buffer, &size, "DISCORD_BOT_TOKEN") != 0 || buffer == nullptr) {
        std::cerr << "Error: environment variable not set.\n";
        return 1;
    }

    std::string token(buffer);
    free(buffer);

    dpp::cluster bot(token);


    bot.on_log(dpp::utility::cout_logger());

    bot.on_ready([&bot](const dpp::ready_t& event) {
        std::cout << "Bot is online!\n";

        // Create a slash command called /ping
        dpp::slashcommand cmd("ping", "Replies with Pong!", bot.me.id);
        bot.global_command_create(cmd);
        });

    bot.on_slashcommand([](const dpp::slashcommand_t& event) {
        if (event.command.get_command_name() == "ping") {
            event.reply("Pong!");
        }
        });

    bot.start(dpp::st_wait);


    return 0;
}
