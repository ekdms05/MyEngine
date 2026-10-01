#include "TestFramework.h"
#include "mye/core/Log.h"
#include "mye/rhi/Rhi.h"
#include <d3d11.h>
#include <dxgi1_2.h>
#include <wrl/client.h>
#include <memory>
#include <string>

namespace {
class DriverSelectionLog final : public mye::ILogSink {
public:
    void Write(const mye::LogMessage& message) override {
        if (message.category == "RHI" && message.message.starts_with("DX11 driver selected:"))
            selection = message.message;
    }
    std::string selection;
};
}

MYE_TEST(Dx11WarpRequiresExplicitSelectionAndRestoresHardwareDefault) {
    struct RestoreTestState {
        std::wstring previous;
        bool existed = false;
        RestoreTestState() {
            const auto length = ::GetEnvironmentVariableW(L"MYE_DX11_WARP", nullptr, 0);
            existed = length != 0;
            if (existed) {
                previous.resize(length);
                ::GetEnvironmentVariableW(L"MYE_DX11_WARP", previous.data(), length);
                previous.resize(length - 1);
            }
        }
        ~RestoreTestState() {
            MYE_EXPECT(::SetEnvironmentVariableW(L"MYE_DX11_WARP", existed ? previous.c_str() : nullptr));
            mye::Log::Shutdown();
        }
    } restore;
    auto sink = std::make_unique<DriverSelectionLog>();
    auto* selected = sink.get();
    mye::Log::AddSink(std::move(sink));
    MYE_EXPECT(::SetEnvironmentVariableW(L"MYE_DX11_WARP", L"10"));
    { auto hardware = mye::rhi::CreateDevice(mye::rhi::Backend::DX11, {}); }
    MYE_EXPECT(selected->selection == "DX11 driver selected: HARDWARE");

    MYE_EXPECT(::SetEnvironmentVariableW(L"MYE_DX11_WARP", L"1"));
    auto warp = mye::rhi::CreateDevice(mye::rhi::Backend::DX11, {});
    MYE_EXPECT(warp);
    MYE_EXPECT(selected->selection == "DX11 driver selected: WARP (MYE_DX11_WARP=1)");
    if (warp) {
        // The public native debug boundary verifies the actual software adapter, not just a log string.
        using Microsoft::WRL::ComPtr;
        ComPtr<IDXGIDevice> dxgi;
        auto* native = static_cast<ID3D11Device*>(warp.Value()->GetNativeDevice());
        MYE_EXPECT(SUCCEEDED(native->QueryInterface(IID_PPV_ARGS(dxgi.ReleaseAndGetAddressOf()))));
        if (dxgi) {
            ComPtr<IDXGIAdapter> adapter;
            MYE_EXPECT(SUCCEEDED(dxgi->GetAdapter(adapter.ReleaseAndGetAddressOf())));
            ComPtr<IDXGIAdapter1> adapter1;
            if (adapter) MYE_EXPECT(SUCCEEDED(adapter.As(&adapter1)));
            if (adapter1) {
                DXGI_ADAPTER_DESC1 description{};
                MYE_EXPECT(SUCCEEDED(adapter1->GetDesc1(&description)));
                MYE_EXPECT((description.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0);
            }
        }
    }
    MYE_EXPECT(::SetEnvironmentVariableW(L"MYE_DX11_WARP", nullptr));
    { auto hardware = mye::rhi::CreateDevice(mye::rhi::Backend::DX11, {}); }
    MYE_EXPECT(selected->selection == "DX11 driver selected: HARDWARE");
}
