// mye/imgui/DebugUi.h — Dear ImGui(docking) win32 + DX11 통합 래퍼 (mye_imgui, docs/07)
//
// 소유: 이 래퍼는 ImGui 컨텍스트 1개와 두 플랫폼/렌더 백엔드(win32·dx11) 수명을 관리한다.
// 배선:
//   - HWND        : Win32Window::GetNativeHandle()
//   - D3D device  : rhi::IDevice::GetNativeDevice()  (ID3D11Device*)
//   - D3D context : rhi::IDevice::GetNativeContext() (ID3D11DeviceContext* immediate)
//   - win32 메시지: Win32Window의 IWindowMessageHook 체인에 ImGui_ImplWin32_WndProcHandler를
//                   높은 선점 우선순위(작은 priority 값)로 등록 → ImGui가 먼저 마우스/키보드를 소비.
//
// 프레임 계약 (docs/01 렌더 루프): 렌더 모듈이 백버퍼 렌더패스로 씬을 그린 뒤,
//   같은 백버퍼를 대상으로 EndFrame()을 호출하면 ImGui 드로우가 그 위에 얹힌다.
// 자세한 사용 순서는 이 헤더 하단 주석 참조.
//
// 예외 불사용(Expected)·dynamic_cast 금지·정적 링크 규약을 따른다.
#pragma once

#include "mye/core/Base.h"

namespace mye {

class IWindow;
namespace win32 { class Win32Window; }
namespace rhi { class IDevice; }

namespace imgui {

class DebugUi {
public:
    DebugUi() = default;
    ~DebugUi();

    DebugUi(const DebugUi&) = delete;
    DebugUi& operator=(const DebugUi&) = delete;

    // ImGui 컨텍스트 생성 + win32/dx11 백엔드 초기화 + 메시지 훅 등록. 도킹 활성.
    // messageHookPriority: 훅 체인 우선순위(작을수록 먼저 = 선점). 기본 -1000(입력 계층보다 앞).
    Expected<void, Error> Initialize(win32::Win32Window& window, rhi::IDevice& device,
                                     int messageHookPriority = -1000);

    // 프레임 시작 — ImplDX11/ImplWin32 NewFrame + ImGui::NewFrame. 이후 ImGui:: 위젯 호출 가능.
    // 초기화 실패 상태면 no-op(true 반환 없이 IsInitialized로 게이트할 것).
    void BeginFrame();

    // 프레임 종료 — ImGui::Render + ImplDX11 RenderDrawData.
    // 반드시 현재 백버퍼가 OMSetRenderTargets로 바인딩된 상태(씬 렌더패스가 열려 있거나 방금 닫혀
    // 백버퍼 RTV가 여전히 바인딩됨)에서 호출한다. ImGui는 자체 뷰포트/씬 상태로 그린다.
    void EndFrame();

    // 백엔드/컨텍스트 해제 + 훅 제거. Initialize의 역순. 디바이스보다 먼저 호출할 것.
    void Shutdown();

    bool IsInitialized() const { return m_initialized; }

private:
    struct HookImpl;   // IWindowMessageHook 구현 은닉 (헤더에 <Windows.h>/imgui 미노출)

    bool                 m_initialized = false;
    win32::Win32Window*  m_window = nullptr;
    void*                m_hwnd = nullptr;      // HWND
    HookImpl*            m_hook = nullptr;      // 소유 (unique_ptr는 불완전 타입 제약으로 raw + 수동 delete)
};

} // namespace imgui
} // namespace mye

// ---------------------------------------------------------------------------
// 사용 순서 (통합 데모 / 렌더 모듈 기준):
//
//   // 1) 초기화 — 디바이스·스왑체인 생성 후 (OnInitialize)
//   mye::imgui::DebugUi debugUi;
//   auto r = debugUi.Initialize(win32Window, *device);   // win32Window: Win32Window&, device: IDevice&
//   if (!r) { /* r.GetError().message 로그 */ }
//
//   // 2) 매 렌더 프레임 (PreRender 틱, device->BeginFrame() 이후)
//   debugUi.BeginFrame();
//     ImGui::Begin("Debug");
//       ImGui::Text("FPS: %.1f", fps);
//       ImGui::Text("Draw calls: %u", drawCalls);
//     ImGui::End();
//   // ...씬 렌더패스: cmd.BeginRenderPass(backbuffer) ... Draw ... (EndRenderPass는 선택)
//   debugUi.EndFrame();            // 백버퍼 RTV 바인딩 상태에서 ImGui 드로우를 얹음
//   swapChain->Present(vsync);
//   device->EndFrame();
//
//   // 3) 종료 (OnShutdown) — 디바이스/스왑체인 파괴 전
//   debugUi.Shutdown();
// ---------------------------------------------------------------------------
