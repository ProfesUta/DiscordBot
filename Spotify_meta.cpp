#include "spotify_meta.h"
#include <curl/curl.h>
#include <nlohmann/json.hpp>
#include <cstdlib>
#include <iostream>

using json = nlohmann::json;

static size_t write_cb(void* data, size_t size, size_t nmemb, void* out) {
    ((std::string*)out)->append((char*)data, size * nmemb);
    return size * nmemb;
}

static std::string get_env(const char* v) {
    char* b = nullptr; size_t s = 0;
    _dupenv_s(&b, &s, v);
    std::string r = b ? b : "";
    free(b);
    return r;
}

static std::string get_access_token() {
    std::string post =
        "grant_type=refresh_token&refresh_token=" + get_env("SPOTIFY_REFRESH_TOKEN") +
        "&client_id=" + get_env("SPOTIFY_CLIENT_ID") +
        "&client_secret=" + get_env("SPOTIFY_CLIENT_SECRET");

    CURL* curl = curl_easy_init();
    std::string out;

    curl_easy_setopt(curl, CURLOPT_URL, "https://accounts.spotify.com/api/token");
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, post.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &out);
    curl_easy_perform(curl);
    curl_easy_cleanup(curl);

    return json::parse(out)["access_token"];
}

SpotifyTrack get_spotify_track(const std::string& url) {
    std::string id = url.substr(url.find("track/") + 6);
    id = id.substr(0, id.find("?"));

    std::string token = get_access_token();

    CURL* curl = curl_easy_init();
    std::string out;

    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, ("Authorization: Bearer " + token).c_str());

    curl_easy_setopt(curl, CURLOPT_URL, ("https://api.spotify.com/v1/tracks/" + id).c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &out);
    curl_easy_perform(curl);

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    auto j = json::parse(out);
    return {
        j["name"],
        j["artists"][0]["name"]
    };
}
