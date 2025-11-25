#pragma once

#include <string>
#include <vector>
#include <iostream>
#include <curl/curl.h>
#include <nlohmann/json.hpp>

#include <functional>
using json = nlohmann::json;

// Callback type for streaming: returns true to continue, false to abort
using StreamCallback = std::function<bool(const std::string&)>;

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

    Response post(const std::string& url, const std::string& data, StreamCallback callback = nullptr) {
        return request(url, data, "POST", callback);
    }

private:
    CURL* curl;

    static size_t WriteCallback(void* contents, size_t size, size_t nmemb, void* userp) {
        size_t totalSize = size * nmemb;
        std::string chunk((char*)contents, totalSize);
        
        // userp is now a pointer to a pair<string*, StreamCallback*>
        auto* data = static_cast<std::pair<std::string*, StreamCallback*>*>(userp);
        data->first->append(chunk);
        
        if (data->second && *data->second) {
            if (!(*data->second)(chunk)) {
                return 0; // Abort
            }
        }
        
        return totalSize;
    }

    Response request(const std::string& url, const std::string& data, const std::string& method, StreamCallback callback = nullptr) {
        Response response;
        if (!curl) {
            response.error = "CURL init failed";
            return response;
        }

        std::string readBuffer;
        std::pair<std::string*, StreamCallback*> callbackData(&readBuffer, &callback);

        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &callbackData);

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

    std::string chat(const std::string& model, const std::vector<Message>& messages, StreamCallback callback = nullptr) {
        json j;
        j["model"] = model;
        j["stream"] = (callback != nullptr);
        
        json msgs = json::array();
        for (const auto& msg : messages) {
            msgs.push_back({{"role", msg.role}, {"content", msg.content}});
        }
        j["messages"] = msgs;

        // Buffer to handle partial JSON chunks
        std::string buffer;

        auto wrappedCallback = [&](const std::string& chunk) -> bool {
            if (!callback) return true;
            
            buffer += chunk;
            size_t pos = 0;
            while ((pos = buffer.find('\n')) != std::string::npos) {
                std::string line = buffer.substr(0, pos);
                buffer.erase(0, pos + 1);
                
                if (line.empty()) continue;
                
                try {
                    auto j = json::parse(line);
                    if (j.contains("message") && j["message"].contains("content")) {
                        std::string content = j["message"]["content"];
                        if (!callback(content)) return false;
                    }
                    if (j.contains("done") && j["done"].get<bool>()) {
                        return true;
                    }
                } catch (...) {
                    // Ignore parse errors for partial lines (shouldn't happen with newline split but safe to ignore)
                }
            }
            return true;
        };

        StreamCallback requestCallback = nullptr;
        if (callback) {
            requestCallback = wrappedCallback;
        }
        auto res = client.post(base_url + "/api/chat", j.dump(), requestCallback);
        
        if (res.status_code == 200) {
            if (callback) {
                // For streaming, we reconstruct the full message from the accumulated body if needed, 
                // but since we processed chunks, the caller might have built it. 
                // However, the 'res.body' contains the full concatenation of JSON objects.
                // We should return the full text.
                std::string full_text;
                std::stringstream ss(res.body);
                std::string line;
                while(std::getline(ss, line)) {
                     try {
                        auto j = json::parse(line);
                        if (j.contains("message") && j["message"].contains("content")) {
                            full_text += j["message"]["content"].get<std::string>();
                        }
                    } catch (...) {}
                }
                return full_text;
            } else {
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
        }
        return "Error: " + res.error;
    }

private:
    std::string base_url;
    HttpClient client;
};
