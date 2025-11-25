#pragma once

#include <string>
#include <vector>
#include <iostream>
#include <curl/curl.h>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

class HttpClient {
public:
    HttpClient() {
        curl = curl_easy_init();
    }

    ~HttpClient() {
        if (curl) {
            curl_easy_cleanup(curl);
        }
    }

    struct Response {
        long status_code;
        std::string body;
        std::string error;
    };

    Response get(const std::string& url) {
        return request(url, "", "GET");
    }

    Response post(const std::string& url, const std::string& data) {
        return request(url, data, "POST");
    }

private:
    CURL* curl;

    static size_t WriteCallback(void* contents, size_t size, size_t nmemb, std::string* userp) {
        size_t totalSize = size * nmemb;
        userp->append((char*)contents, totalSize);
        return totalSize;
    }

    Response request(const std::string& url, const std::string& data, const std::string& method) {
        Response response;
        if (!curl) {
            response.error = "CURL init failed";
            return response;
        }

        std::string readBuffer;
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &readBuffer);

        struct curl_slist* headers = NULL;
        headers = curl_slist_append(headers, "Content-Type: application/json");
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

        if (method == "POST") {
            curl_easy_setopt(curl, CURLOPT_POST, 1L);
            curl_easy_setopt(curl, CURLOPT_POSTFIELDS, data.c_str());
        } else {
            curl_easy_setopt(curl, CURLOPT_POST, 0L);
        }

        CURLcode res = curl_easy_perform(curl);
        if (res != CURLE_OK) {
            response.error = curl_easy_strerror(res);
        } else {
            curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response.status_code);
            response.body = readBuffer;
        }

        curl_slist_free_all(headers);
        // Reset options for next request
        curl_easy_reset(curl);
        
        return response;
    }
};

struct ModelInfo {
    std::string name;
};

struct Message {
    std::string role;
    std::string content;
};

class Ollama {
public:
    Ollama(const std::string& base_url = "http://localhost:11434") : base_url(base_url) {}

    std::vector<std::string> list_models() {
        auto res = client.get(base_url + "/api/tags");
        std::vector<std::string> models;
        if (res.status_code == 200) {
            try {
                auto j = json::parse(res.body);
                for (const auto& model : j["models"]) {
                    models.push_back(model["name"]);
                }
            } catch (const std::exception& e) {
                std::cerr << "JSON Parse Error: " << e.what() << std::endl;
            }
        } else {
            std::cerr << "Failed to get models: " << res.error << " (Status: " << res.status_code << ")" << std::endl;
        }
        return models;
    }

    std::string chat(const std::string& model, const std::vector<Message>& messages) {
        json j;
        j["model"] = model;
        j["stream"] = false;
        
        json msgs = json::array();
        for (const auto& msg : messages) {
            msgs.push_back({{"role", msg.role}, {"content", msg.content}});
        }
        j["messages"] = msgs;

        auto res = client.post(base_url + "/api/chat", j.dump());
        if (res.status_code == 200) {
            try {
                auto resp_j = json::parse(res.body);
                if (resp_j.contains("message")) {
                    return resp_j["message"]["content"];
                } else if (resp_j.contains("error")) {
                    return "Error: " + resp_j["error"].get<std::string>();
                }
            } catch (const std::exception& e) {
                return "JSON Parse Error: " + std::string(e.what());
            }
        }
        return "Error: " + res.error;
    }

private:
    std::string base_url;
    HttpClient client;
};
