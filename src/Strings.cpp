#include "Strings.h"
#include "dao/Log.h"

namespace transmog::strings
{

namespace
{
dao::strings::Table g_table;
}

void Init(const dao::LoaderInfo &info)
{
    g_table = dao::strings::Table::Load(info, L"DAOTransmog");
    if (g_table.File())
    {
        spdlog::info("[DAOTransmog] Loaded talk table from {}", g_table.File()->string());
    }
    else
    {
        spdlog::warn("[DAOTransmog] Could not load DAOTransmog_<locale>.tlk; using fallback strings");
    }
}

std::wstring Get(StrId id, std::wstring_view fallback)
{
    const auto res = g_table.String(static_cast<std::uint32_t>(id), fallback);
    if (!res.empty())
    {
        return res;
    }
    return std::wstring(fallback);
}

} // namespace transmog::strings
