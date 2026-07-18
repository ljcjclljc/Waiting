#pragma once

#include <drogon/drogon.h>

#include <string>

namespace blog
{
inline drogon::HttpResponsePtr jsonResponse(
    Json::Value body,
    drogon::HttpStatusCode status = drogon::k200OK)
{
    auto response = drogon::HttpResponse::newHttpJsonResponse(std::move(body));
    response->setStatusCode(status);
    return response;
}

inline drogon::HttpResponsePtr jsonError(
    const std::string &message,
    drogon::HttpStatusCode status,
    Json::Value fields = Json::Value(Json::objectValue))
{
    Json::Value body;
    body["ok"] = false;
    body["message"] = message;
    body["fields"] = std::move(fields);
    return jsonResponse(std::move(body), status);
}

inline drogon::HttpResponsePtr viewError(const std::string &message,
                                         drogon::HttpStatusCode status)
{
    drogon::HttpViewData data;
    data.insert("message", message);
    auto response = drogon::HttpResponse::newHttpViewResponse("Error", data);
    response->setStatusCode(status);
    return response;
}
}  // namespace blog
