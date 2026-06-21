#include <windows.h>
#include <ole2.h>
#include <comutil.h>
#include <string>
#include <vector>
#include <iostream>
#include <exception>
#include <nlohmann/json.hpp>

#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "oleaut32.lib")

using json = nlohmann::json;

// 插件接口导出
extern "C" {
    __declspec(dllexport) const char* get_plugin_name();
    __declspec(dllexport) const char* get_tool_name();
    __declspec(dllexport) const char* get_tool_description();
    __declspec(dllexport) const char* get_tool_parameters();
    __declspec(dllexport) const char* execute_tool(const char* json_args);
    __declspec(dllexport) void cleanup_tool();
}

// ============================================================================
// 核心逻辑实现
// ============================================================================

const char* get_plugin_name() { return "WordToolPlugin"; }
const char* get_tool_name() { return "word_editor"; }
const char* get_tool_description() { return "使用 Microsoft Word 读取或写入 .docx 文件"; }
const char* get_tool_parameters() {
    return R"({
        "type": "object",
        "properties": {
            "action": {"type": "string", "enum": ["read", "write"]},
            "file_path": {"type": "string"},
            "text": {"type": "string", "description": "写入文件的内容"}
        },
        "required": ["action", "file_path"]
    })";
}

// 实际的 Word 操作逻辑 (使用 COM 自动化)
std::string perform_word_action(const json& args) {
    std::string action = args["action"];
    std::string path = args["file_path"];

    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    
    try {
        CLSID clsid;
        HRESULT hr = CLSIDFromProgID(L"Word.Application", &clsid);
        if (FAILED(hr)) throw std::runtime_error("未找到 Microsoft Word，请确保已安装。");

        IDispatch* pWordApp = nullptr;
        hr = CoCreateInstance(clsid, NULL, CLSCTX_LOCAL_SERVER, IID_IDispatch, (void**)&pWordApp);
        if (FAILED(hr)) throw std::runtime_error("无法启动 Word 进程。");

        // 获取 Documents 集合
        DISPATCH* pDispDocs = nullptr;
        DISPID dispidDocs;
        pWordApp->GetIDsOfNames(IID_NULL, L"Documents", 1, LOCALE_USER_DEFAULT, &dispidDocs);
        hr = pWordApp->Invoke(dispidDocs, DISPATCH_METHOD, VT_DISPATCH, &pDispDocs, nullptr);
        if (FAILED(hr)) throw std::runtime_error("Failed to access Documents.");

        if (action == "read") {
            DISPATCH* pDispDoc = nullptr;
            DISPID dispidOpen;
            pDispDocs->GetIDsOfNames(IID_NULL, L"Open", 1, LOCALE_USER_DEFAULT, &dispidOpen);
            _bstr_t bstrPath(path.c_str());
            _variant_t varPath(bstrPath);
            hr = pDispDocs->Invoke(dispidOpen, DISPATCH_METHOD, VT_DISPATCH, &pDispDoc, &varPath);
            if (FAILED(hr) || !pDispDoc) throw std::runtime_error("无法打开文件: " + path);

            DISPATCH* pDispContent = nullptr;
            DISPID dispidContent;
            pDispDoc->GetIDsOfNames(IID_NULL, L"Content", 1, LOCALE_USER_DEFAULT, &dispidContent);
            pDispDoc->Invoke(dispidContent, DISPATCH_PROPERTYGET, VT_DISPATCH, &pDispContent, nullptr);

            _variant_t varText;
            DISPID dispidText;
            pDispContent->GetIDsOfNames(IID_NULL, L"Text", 1, LOCALE_USER_DEFAULT, &dispidText);
            pDispContent->Invoke(dispidText, DISPATCH_PROPERTYGET, VT_BSTR, &varText);

            std::string result = (const char*)varText.bstrVal;

            pDispContent->Release();
            pDispDoc->Release();
            pDispDocs->Release();
            pWordApp->Release();
            CoUninitialize();
            return result;

        } else if (action == "write") {
            DISPATCH* pDispDoc = nullptr;
            DISPID dispidAdd;
            pDispDocs->GetIDsOfNames(IID_NULL, L"Add", 1, LOCALE_USER_DEFAULT, &dispidAdd);
            pDispDocs->Invoke(dispidAdd, DISPATCH_METHOD, VT_DISPATCH, &pDispDoc, nullptr);

            DISPATCH* pDispRange = nullptr;
            DISPID dispidRange;
            pDispDoc->GetIDsOfNames(IID_NULL, L"Range", 1, LOCALE_USER_DEFAULT, &dispidRange);
            pDispDoc->Invoke(dispidRange, DISPATCH_PROPERTYGET, VT_DISPATCH, &pDispRange, nullptr);

            DISPID dispidSetText;
            pDispRange->GetIDsOfNames(IID_NULL, L"Text", 1, LOCALE_USER_DEFAULT, &dispidSetText);
            _bstr_t bstrContent(args.value("text", "").c_str());
            _variant_t varContent(bstrContent);
            pDispRange->Invoke(dispidSetText, DISPATCH_PROPERTYPUT, VT_BSTR, nullptr, &varContent);

            DISPID dispidSaveAs;
            pDispDoc->GetIDsOfNames(IID_NULL, L"SaveAs2", 1, LOCALE_USER_DEFAULT, &dispidSaveAs);
            _bstr_t bstrSavePath(path.c_str());
            _variant_t varSavePath(bstrSavePath);
            pDispDoc->Invoke(dispidSaveAs, DISPATCH_METHOD, VT_EMPTY, nullptr, &varSavePath);

            DISPID dispidClose;
            pDispDoc->GetIDsOfNames(IID_NULL, L"Close", 1, LOCALE_USER_DEFAULT, &dispidClose);
            pDispDoc->Invoke(dispidClose, DISPATCH_METHOD, VT_EMPTY, nullptr, nullptr);

            pDispRange->Release();
            pDispDoc->Release();
            pDispDocs->Release();
            pWordApp->Release();
            CoUninitialize();
            return "Success: File written to " + path;
        }
    } catch (const std::exception& e) {
        CoUninitialize();
        return std::string("Error: ") + e.what();
    } catch (...) {
        CoUninitialize();
        return "Error: Unknown COM error occurred.";
    }
    return "Error: Invalid action.";
}

const char* execute_tool(const char* json_args) {
    try {
        json args = json::parse(json_args);
        std::string result = perform_word_action(args);
        char* res = new char[result.size() + 1];
        strcpy(res, result.c_str());
        return res; 
    } catch (const std::exception& e) {
        return strdup(e.what());
    }
}

void cleanup_tool() {}
