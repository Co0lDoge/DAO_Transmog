#include "TestBridge.h"
#include "GuiBridge.h"
#include "State.h"
#include "dao/ClientExoApp.h"
#include "dao/Frame.h"
#include "dao/GameModes.h"
#include "dao/Gui.h"
#include "dao/Messages.h"
#include "dao/ObjectId.h"
#include "dao/Party.h"
#include "dao/Saves.h"
#include "dao/generated/ScriptApi.h"

#include <windows.h>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <future>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <vector>

namespace transmog::testbridge
{
namespace
{

constexpr const char *kPipeName = "\\\\.\\pipe\\dao_mcp_bridge";
constexpr DWORD kBufferSize = 65536;

struct BridgeTask
{
    nlohmann::json request;
    std::promise<nlohmann::json> responsePromise;
};

std::atomic<bool> g_running{false};
std::thread g_serverThread;
std::mutex g_queueMutex;
std::vector<std::shared_ptr<BridgeTask>> g_taskQueue;

HWND GetGameWindow()
{
    static HWND s_hwnd = nullptr;
    if (s_hwnd != nullptr && IsWindow(s_hwnd))
    {
        return s_hwnd;
    }

    const DWORD currentPid = GetCurrentProcessId();
    EnumWindows([](HWND hwnd, LPARAM lParam) -> BOOL {
        DWORD pid = 0;
        GetWindowThreadProcessId(hwnd, &pid);
        if (pid == static_cast<DWORD>(lParam) && IsWindowVisible(hwnd))
        {
            RECT rc;
            GetClientRect(hwnd, &rc);
            if (rc.right > 200 && rc.bottom > 200)
            {
                s_hwnd = hwnd;
                return FALSE; // Found
            }
        }
        return TRUE;
    }, static_cast<LPARAM>(currentPid));

    return s_hwnd;
}

bool CaptureWindowToBmp(HWND hwnd, const std::string &filePath, int &outWidth, int &outHeight)
{
    if (hwnd == nullptr || !IsWindow(hwnd))
    {
        return false;
    }

    RECT rc;
    if (!GetClientRect(hwnd, &rc))
    {
        return false;
    }

    const int width = rc.right - rc.left;
    const int height = rc.bottom - rc.top;
    if (width <= 0 || height <= 0)
    {
        return false;
    }

    outWidth = width;
    outHeight = height;

    HDC hdcWindow = GetDC(hwnd);
    if (hdcWindow == nullptr)
    {
        return false;
    }

    HDC hdcMemDC = CreateCompatibleDC(hdcWindow);
    HBITMAP hbm = CreateCompatibleBitmap(hdcWindow, width, height);
    HGDIOBJ hOldBmp = SelectObject(hdcMemDC, hbm);

    BOOL res = PrintWindow(hwnd, hdcMemDC, PW_CLIENTONLY);
    if (!res)
    {
        BitBlt(hdcMemDC, 0, 0, width, height, hdcWindow, 0, 0, SRCCOPY);
    }

    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = width;
    bmi.bmiHeader.biHeight = height; // bottom-up
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 24;
    bmi.bmiHeader.biCompression = BI_RGB;

    const DWORD rowSize = ((width * 3 + 3) & ~3);
    const DWORD imageSize = rowSize * height;
    std::vector<BYTE> pixels(imageSize, 0);

    GetDIBits(hdcMemDC, hbm, 0, height, pixels.data(), &bmi, DIB_RGB_COLORS);

    BITMAPFILEHEADER bfh = {};
    bfh.bfType = 0x4D42; // "BM"
    bfh.bfOffBits = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER);
    bfh.bfSize = bfh.bfOffBits + imageSize;

    std::ofstream out(filePath, std::ios::binary);
    if (!out)
    {
        SelectObject(hdcMemDC, hOldBmp);
        DeleteObject(hbm);
        DeleteDC(hdcMemDC);
        ReleaseDC(hwnd, hdcWindow);
        return false;
    }

    out.write(reinterpret_cast<const char *>(&bfh), sizeof(bfh));
    out.write(reinterpret_cast<const char *>(&bmi.bmiHeader), sizeof(bmi.bmiHeader));
    out.write(reinterpret_cast<const char *>(pixels.data()), imageSize);

    SelectObject(hdcMemDC, hOldBmp);
    DeleteObject(hbm);
    DeleteDC(hdcMemDC);
    ReleaseDC(hwnd, hdcWindow);
    return true;
}

bool TakeInEngineScreenshot()
{
    auto *app = dao::CClientExoApp::Get();
    auto *modes = app != nullptr ? app->GameModes() : nullptr;
    if (modes == nullptr)
    {
        return false;
    }

    auto shotMsg = dao::messages::Make<dao::messages::TakeScreenshotMessage>();
    return dao::messages::Send(modes, shotMsg);
}

std::wstring Utf8ToWide(std::string_view utf8Str)
{
    if (utf8Str.empty())
    {
        return {};
    }
    const int count = MultiByteToWideChar(CP_UTF8, 0, utf8Str.data(), static_cast<int>(utf8Str.size()), nullptr, 0);
    if (count <= 0)
    {
        return {};
    }
    std::wstring result(count, 0);
    MultiByteToWideChar(CP_UTF8, 0, utf8Str.data(), static_cast<int>(utf8Str.size()), result.data(), count);
    return result;
}

std::string WideToUtf8(std::wstring_view wstr)
{
    if (wstr.empty())
    {
        return {};
    }
    const int size =
        WideCharToMultiByte(CP_UTF8, 0, wstr.data(), static_cast<int>(wstr.size()), nullptr, 0, nullptr, nullptr);
    if (size <= 0)
    {
        return {};
    }
    std::string result(size, 0);
    WideCharToMultiByte(CP_UTF8, 0, wstr.data(), static_cast<int>(wstr.size()), result.data(), size, nullptr, nullptr);
    return result;
}

nlohmann::json ExecuteCommandOnMainThread(const nlohmann::json &req)
{
    const std::string method = req.value("method", "");
    const auto params = req.value("params", nlohmann::json::object());

    nlohmann::json resp = nlohmann::json::object();
    resp["id"] = req.value("id", 0);

    if (method == "ping")
    {
        nlohmann::json res;
        res["status"] = "ok";
        res["game_running"] = true;
        res["frame_count"] = dao::FrameCount();

        const auto modeOpt = dao::CClientExoApp::CurrentMode();
        if (modeOpt.has_value())
        {
            const auto m = modeOpt.value();
            res["game_mode"] = static_cast<int>(m);
            res["is_in_world"] = (m == dao::GameMode::Explore || m == dao::GameMode::Combat || m == dao::GameMode::Gui);
            res["is_loading"] = (m == dao::GameMode::Loading);
            res["is_pregame"] = (m == dao::GameMode::Pregame);
        }
        else
        {
            res["game_mode"] = -1;
            res["is_in_world"] = false;
            res["is_loading"] = false;
            res["is_pregame"] = false;
        }

        auto movie = dao::gui::ScreenMovie(L"Inventory");
        res["inventory_open"] = static_cast<bool>(movie);
        res["browser_visible"] = transmog::gui::IsBrowserVisible();
        resp["result"] = res;
        return resp;
    }

    if (method == "list_saves")
    {
        std::wstring curChar = dao::saves::Character();
        if (curChar.empty())
        {
            const auto chars = dao::saves::Characters();
            if (!chars.empty())
            {
                curChar = chars.front();
            }
        }
        const auto slots = dao::saves::List(curChar);
        nlohmann::json savesJson = nlohmann::json::array();
        for (std::size_t i = 0; i < slots.size(); ++i)
        {
            const auto &s = slots[i];
            savesJson.push_back({
                {"index", i},
                {"character", WideToUtf8(s.character)},
                {"number", s.number},
                {"kind", static_cast<int>(s.kind)},
                {"kind_name", std::string(dao::saves::Name(s.kind))}
            });
        }
        resp["result"] = {
            {"character", WideToUtf8(curChar)},
            {"count", slots.size()},
            {"saves", savesJson}
        };
        return resp;
    }

    if (method == "load_save")
    {
        std::wstring curChar = dao::saves::Character();
        if (curChar.empty())
        {
            const auto chars = dao::saves::Characters();
            if (!chars.empty())
            {
                curChar = chars.front();
            }
        }
        const auto slots = dao::saves::List(curChar);
        if (slots.empty())
        {
            resp["error"] = {{"code", -32003}, {"message", "No save slots found for current character"}};
            return resp;
        }

        std::size_t slotIdx = 0;
        if (params.contains("index"))
        {
            slotIdx = static_cast<std::size_t>(params.value("index", 0));
        }

        if (slotIdx >= slots.size())
        {
            resp["error"] = {{"code", -32004}, {"message", "Save slot index out of bounds"}};
            return resp;
        }

        const bool accepted = dao::saves::Load(slots[slotIdx]);
        resp["result"] = {
            {"accepted", accepted},
            {"slot_number", slots[slotIdx].number},
            {"character", WideToUtf8(slots[slotIdx].character)}
        };
        return resp;
    }

    if (method == "force_load")
    {
        const bool accepted = dao::saves::ForceLoad();
        resp["result"] = {{"accepted", accepted}};
        return resp;
    }

    if (method == "save_game")
    {
        const std::string kindStr = params.value("kind", "quick");
        const std::string nameStr = params.value("name", "TempTest");
        dao::saves::Kind kind = dao::saves::Kind::Quick;
        if (kindStr == "manual")
        {
            kind = dao::saves::Kind::Manual;
        }
        else if (kindStr == "auto")
        {
            kind = dao::saves::Kind::Auto;
        }
        const int slotNum = params.value("slot", -1);
        const bool accepted = dao::saves::Save(kind, slotNum, Utf8ToWide(nameStr));
        resp["result"] = {
            {"accepted", accepted},
            {"kind", kindStr},
            {"slot", slotNum},
            {"name", nameStr}
        };
        return resp;
    }

    if (method == "quit_game")
    {
        HWND hwnd = GetGameWindow();
        if (hwnd != nullptr)
        {
            PostMessageA(hwnd, WM_CLOSE, 0, 0);
            resp["result"] = {{"success", true}};
        }
        else
        {
            resp["error"] = {{"code", -32001}, {"message", "Game window not found"}};
        }
        return resp;
    }

    if (method == "open_screen")
    {
        const std::string screenName = params.value("screen", "Inventory");
        const std::wstring screenNameW = Utf8ToWide(screenName);
        bool ok = false;
        if (screenName == "Inventory")
        {
            auto movie = dao::gui::ScreenMovie(L"Inventory");
            if (!movie)
            {
                ok = dao::messages::SendBinding(L"ToggleFullScreenGUIMessage Inventory");
            }
            else
            {
                ok = true;
            }
        }
        else
        {
            ok = dao::gui::ShowScreen(screenNameW);
        }
        resp["result"] = {{"success", ok}, {"screen", screenName}};
        return resp;
    }

    if (method == "close_screen")
    {
        const std::string screenName = params.value("screen", "Inventory");
        const std::wstring screenNameW = Utf8ToWide(screenName);
        bool ok = false;
        if (screenName == "Inventory")
        {
            auto movie = dao::gui::ScreenMovie(L"Inventory");
            if (movie)
            {
                ok = dao::messages::SendBinding(L"ToggleFullScreenGUIMessage Inventory");
            }
            else
            {
                ok = true;
            }
        }
        else
        {
            ok = dao::gui::CloseScreen(screenNameW);
        }
        resp["result"] = {{"success", ok}, {"screen", screenName}};
        return resp;
    }

    if (method == "inspect_ui")
    {
        const std::string path = params.value("path", "");
        const auto val = transmog::gui::QueryInventoryUiVariable(path.c_str());
        nlohmann::json res;
        res["found"] = val.found;
        res["type"] = val.type;
        if (val.type == "bool")
        {
            res["value"] = val.boolVal;
        }
        else if (val.type == "number")
        {
            res["value"] = val.numVal;
        }
        else if (val.type == "string")
        {
            res["value"] = val.strVal;
        }
        resp["result"] = res;
        return resp;
    }

    if (method == "inspect_ui_batch")
    {
        const auto paths = params.value("paths", std::vector<std::string>{});
        nlohmann::json values = nlohmann::json::object();
        for (const auto &p : paths)
        {
            const auto val = transmog::gui::QueryInventoryUiVariable(p.c_str());
            if (!val.found)
            {
                values[p] = nullptr;
            }
            else if (val.type == "bool")
            {
                values[p] = val.boolVal;
            }
            else if (val.type == "number")
            {
                values[p] = val.numVal;
            }
            else
            {
                values[p] = val.strVal;
            }
        }
        resp["result"] = {{"values", values}};
        return resp;
    }

    if (method == "trigger_action")
    {
        const std::string command = params.value("command", "");
        const std::string arg = params.value("arg", "");
        const bool executed = transmog::gui::SimulateUiAction(command, arg);
        resp["result"] = {{"command", command}, {"arg", arg}, {"executed", executed}};
        return resp;
    }

    if (method == "get_state")
    {
        const auto charId = transmog::gui::GetCurrentCharacterId();
        nlohmann::json res;
        res["hero_id"] = static_cast<std::uint32_t>(charId);
        auto movie = dao::gui::ScreenMovie(L"Inventory");
        res["inventory_open"] = static_cast<bool>(movie);
        res["browser_visible"] = transmog::gui::IsBrowserVisible();

        try
        {
            res["characters"] = nlohmann::json::parse(state::ToJson());
        }
        catch (...)
        {
            res["characters"] = nlohmann::json::object();
        }
        resp["result"] = res;
        return resp;
    }

    if (method == "get_party")
    {
        const auto controlled = dao::party::Controlled();
        const auto screenChar = dao::party::ScreenCharacter();
        std::vector<dao::ObjectId> members;

        const auto partyArray = dao::script::api::GetPartyList(controlled);
        for (std::size_t i = 0; i < partyArray.Size(); ++i)
        {
            const auto obj = partyArray.Object(i);
            if (obj != dao::kInvalidObject)
            {
                members.push_back(obj);
            }
        }
        if (members.empty())
        {
            members = dao::party::Selected();
        }
        if (members.empty() && controlled != dao::kInvalidObject)
        {
            members.push_back(controlled);
        }

        nlohmann::json memberList = nlohmann::json::array();
        for (const auto m : members)
        {
            nlohmann::json info;
            info["id"] = static_cast<std::uint32_t>(m);
            info["appearance_type"] = dao::script::api::GetAppearanceType(m);
            info["racial_type"] = dao::script::api::GetCreatureRacialType(m);
            info["gender"] = dao::script::api::GetCreatureGender(m);
            info["is_controlled"] = (m == controlled);
            info["is_screen"] = (m == screenChar);
            memberList.push_back(info);
        }
        resp["result"] = {
            {"controlled", static_cast<std::uint32_t>(controlled)},
            {"screen_character", static_cast<std::uint32_t>(screenChar)},
            {"members", memberList}
        };
        return resp;
    }

    if (method == "next_character")
    {
        const bool sent = dao::messages::SendText(L"GUI.SelectNextCharacterMessage");
        const auto newScreen = dao::party::ScreenCharacter();
        resp["result"] = {
            {"success", sent},
            {"screen_character", static_cast<std::uint32_t>(newScreen)}
        };
        return resp;
    }

    if (method == "prev_character")
    {
        const bool sent = dao::messages::SendText(L"GUI.SelectPreviousCharacterMessage");
        const auto newScreen = dao::party::ScreenCharacter();
        resp["result"] = {
            {"success", sent},
            {"screen_character", static_cast<std::uint32_t>(newScreen)}
        };
        return resp;
    }

    if (method == "set_screen_character")
    {
        dao::ObjectId target = dao::kInvalidObject;
        if (params.contains("character_id"))
        {
            target = static_cast<dao::ObjectId>(params["character_id"].get<std::uint32_t>());
        }
        else if (params.contains("index"))
        {
            const auto idx = params["index"].get<std::size_t>();
            const auto members = dao::party::Selected();
            if (idx < members.size())
            {
                target = members[idx];
            }
        }
        if (target == dao::kInvalidObject)
        {
            resp["error"] = {{"code", -32602}, {"message", "Invalid character target"}};
            return resp;
        }
        const bool success = dao::party::SetScreenCharacter(target);
        resp["result"] = {
            {"success", success},
            {"character_id", static_cast<std::uint32_t>(target)},
            {"screen_character", static_cast<std::uint32_t>(dao::party::ScreenCharacter())}
        };
        return resp;
    }

    if (method == "inspect_creature")
    {
        dao::ObjectId target = dao::kInvalidObject;
        if (params.contains("character_id"))
        {
            target = static_cast<dao::ObjectId>(params["character_id"].get<std::uint32_t>());
        }
        else
        {
            target = transmog::gui::GetCurrentCharacterId();
        }
        nlohmann::json res;
        res["character_id"] = static_cast<std::uint32_t>(target);
        if (target != dao::kInvalidObject)
        {
            res["appearance_type"] = dao::script::api::GetAppearanceType(target);
            res["racial_type"] = dao::script::api::GetCreatureRacialType(target);
            res["gender"] = dao::script::api::GetCreatureGender(target);

            nlohmann::json equipSlots = nlohmann::json::object();
            for (int s = 0; s <= 20; ++s)
            {
                const auto itemId = dao::script::api::GetItemInEquipSlot(s, target);
                if (itemId != dao::kInvalidObject)
                {
                    equipSlots[std::to_string(s)] = static_cast<std::uint32_t>(itemId);
                }
            }
            res["equipped_items"] = equipSlots;
        }
        resp["result"] = res;
        return resp;
    }

    if (method == "send_key")
    {
        const std::string key = params.value("key", "");
        HWND hwnd = GetGameWindow();
        if (hwnd == nullptr)
        {
            resp["error"] = {{"code", -32001}, {"message", "Game window not found"}};
            return resp;
        }

        UINT vk = 0;
        if (key == "I" || key == "i")
        {
            vk = 'I';
        }
        else if (key == "T" || key == "t")
        {
            vk = 'T';
        }
        else if (key == "H" || key == "h")
        {
            vk = 'H';
        }
        else if (key == "P" || key == "p")
        {
            vk = 'P';
        }
        else if (key == "ESC" || key == "Escape")
        {
            vk = VK_ESCAPE;
        }
        else if (key == "TAB" || key == "Tab")
        {
            vk = VK_TAB;
        }
        else if (key == "PageUp")
        {
            vk = VK_PRIOR;
        }
        else if (key == "PageDown")
        {
            vk = VK_NEXT;
        }

        if (vk != 0)
        {
            if (key == "T" || key == "t")
            {
                auto movie = dao::gui::ScreenMovie(L"Inventory");
                if (movie)
                {
                    transmog::gui::ToggleWardrobeBrowser();
                }
            }

            keybd_event(static_cast<BYTE>(vk), 0, 0, 0);
            keybd_event(static_cast<BYTE>(vk), 0, KEYEVENTF_KEYUP, 0);
            PostMessageA(hwnd, WM_KEYDOWN, vk, 0);
            PostMessageA(hwnd, WM_KEYUP, vk, 0);
            resp["result"] = {{"sent_key", key}, {"vk", vk}};
        }
        else
        {
            resp["error"] = {{"code", -32602}, {"message", "Unsupported key"}};
        }
        return resp;
    }

    if (method == "take_screenshot")
    {
        const std::string filePath = params.value("filepath", "build/test_screenshots/capture.jpg");
        bool success = TakeInEngineScreenshot();
        int width = 1920;
        int height = 1080;
        if (!success)
        {
            HWND hwnd = GetGameWindow();
            success = CaptureWindowToBmp(hwnd, filePath, width, height);
        }
        resp["result"] = {
            {"success", success},
            {"filepath", filePath},
            {"width", width},
            {"height", height}
        };
        return resp;
    }

    resp["error"] = {{"code", -32601}, {"message", "Method not found: " + method}};
    return resp;
}

void OnGameFrame()
{
    std::vector<std::shared_ptr<BridgeTask>> localQueue;
    {
        std::lock_guard<std::mutex> lock(g_queueMutex);
        if (g_taskQueue.empty())
        {
            return;
        }
        localQueue.swap(g_taskQueue);
    }

    for (auto &task : localQueue)
    {
        try
        {
            auto resp = ExecuteCommandOnMainThread(task->request);
            task->responsePromise.set_value(std::move(resp));
        }
        catch (const std::exception &e)
        {
            nlohmann::json err;
            err["id"] = task->request.value("id", 0);
            err["error"] = {{"code", -32000}, {"message", e.what()}};
            task->responsePromise.set_value(std::move(err));
        }
        catch (...)
        {
            nlohmann::json err;
            err["id"] = task->request.value("id", 0);
            err["error"] = {{"code", -32000}, {"message", "Unknown exception during execution"}};
            task->responsePromise.set_value(std::move(err));
        }
    }
}

void PipeServerWorker()
{
    spdlog::info("[DAOTransmog::TestBridge] Named pipe listener started on {}", kPipeName);

    while (g_running.load())
    {
        HANDLE hPipe = CreateNamedPipeA(
            kPipeName,
            PIPE_ACCESS_DUPLEX,
            PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS,
            PIPE_UNLIMITED_INSTANCES,
            kBufferSize,
            kBufferSize,
            5000,
            nullptr
        );

        if (hPipe == INVALID_HANDLE_VALUE)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
            continue;
        }

        const BOOL connected = ConnectNamedPipe(hPipe, nullptr) ? TRUE : (GetLastError() == ERROR_PIPE_CONNECTED);
        if (!connected)
        {
            CloseHandle(hPipe);
            continue;
        }

        spdlog::info("[DAOTransmog::TestBridge] Client connected to {}", kPipeName);

        std::vector<char> buffer(kBufferSize);
        while (g_running.load())
        {
            DWORD bytesRead = 0;
            const BOOL readOk = ReadFile(hPipe, buffer.data(), static_cast<DWORD>(buffer.size() - 1), &bytesRead, nullptr);
            if (!readOk || bytesRead == 0)
            {
                break;
            }

            buffer[bytesRead] = '\0';
            std::string reqStr(buffer.data(), bytesRead);

            nlohmann::json jReq;
            try
            {
                jReq = nlohmann::json::parse(reqStr);
            }
            catch (const std::exception &e)
            {
                nlohmann::json errResp;
                errResp["error"] = {{"code", -32700}, {"message", std::string("Parse error: ") + e.what()}};
                std::string errStr = errResp.dump() + "\n";
                DWORD bytesWritten = 0;
                WriteFile(hPipe, errStr.data(), static_cast<DWORD>(errStr.size()), &bytesWritten, nullptr);
                continue;
            }

            auto task = std::make_shared<BridgeTask>();
            task->request = std::move(jReq);
            auto fut = task->responsePromise.get_future();

            {
                std::lock_guard<std::mutex> lock(g_queueMutex);
                g_taskQueue.push_back(task);
            }

            // Wait for main game thread to execute the command
            if (fut.wait_for(std::chrono::seconds(5)) == std::future_status::ready)
            {
                auto jResp = fut.get();
                std::string respStr = jResp.dump() + "\n";
                DWORD bytesWritten = 0;
                WriteFile(hPipe, respStr.data(), static_cast<DWORD>(respStr.size()), &bytesWritten, nullptr);
            }
            else
            {
                nlohmann::json timeoutResp;
                timeoutResp["id"] = task->request.value("id", 0);
                timeoutResp["error"] = {{"code", -32002}, {"message", "Command timed out waiting for game frame"}};
                std::string timeoutStr = timeoutResp.dump() + "\n";
                DWORD bytesWritten = 0;
                WriteFile(hPipe, timeoutStr.data(), static_cast<DWORD>(timeoutStr.size()), &bytesWritten, nullptr);
            }
        }

        DisconnectNamedPipe(hPipe);
        CloseHandle(hPipe);
    }

    spdlog::info("[DAOTransmog::TestBridge] Named pipe listener stopped");
}

} // namespace

void Init()
{
    if (g_running.exchange(true))
    {
        return;
    }

    dao::OnFrame(&OnGameFrame);
    g_serverThread = std::thread(&PipeServerWorker);
    spdlog::info("[DAOTransmog::TestBridge] TestBridge initialized");
}

void Shutdown()
{
    if (!g_running.exchange(false))
    {
        return;
    }

    // Connect briefly to unblock ConnectNamedPipe
    HANDLE hPipe = CreateFileA(kPipeName, GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
    if (hPipe != INVALID_HANDLE_VALUE)
    {
        CloseHandle(hPipe);
    }

    if (g_serverThread.joinable())
    {
        g_serverThread.join();
    }
}

} // namespace transmog::testbridge
