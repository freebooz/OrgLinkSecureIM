#include "model/SettingsModel.h"

#include <utility>

namespace orglink::client
{

void SettingsModel::replaceProfile(SettingsProfileItem profile)
{
    profile_ = std::move(profile);
    emit profileChanged(profile_);
}

void SettingsModel::replaceSystemInfo(SettingsSystemInfoItem systemInfo)
{
    systemInfo_ = std::move(systemInfo);
    emit systemInfoChanged(systemInfo_);
}

void SettingsModel::clear()
{
    replaceProfile({});
    replaceSystemInfo({});
}

} // namespace orglink::client
