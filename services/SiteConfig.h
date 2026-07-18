#pragma once

#include <drogon/drogon.h>

namespace blog
{
inline Json::Value siteConfig()
{
    const auto &custom = drogon::app().getCustomConfig();
    return custom.isMember("site") ? custom["site"] : Json::Value(Json::objectValue);
}
}  // namespace blog
