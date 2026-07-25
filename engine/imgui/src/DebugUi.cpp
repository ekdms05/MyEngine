// mye/imgui/DebugUi.cpp — Dear ImGui(docking) win32 + DX11 백엔드 배선 (mye_imgui)
#include "mye/imgui/DebugUi.h"
#include "mye/imgui/ImGuiSkin.h"
#include "mye/imgui/ImGuiFonts.h"

#include "mye/core/Input.h"
#include "mye/core/Window.h"
#include "mye/core/platform/Win32Window.h"
#include "mye/rhi/Rhi.h"

#include "imgui.h"
#include "backends/imgui_impl_win32.h"
#include "backends/imgui_impl_dx11.h"

#include <d3d11.h>
#include <Windows.h>

// ImGui win32 백엔드가 제공하는 WndProc 핸들러(전방 선언은 imgui_impl_win32.h에 있음).
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg,
                                                             WPARAM wParam, LPARAM lParam);

namespace mye::imgui {

// ---------------------------------------------------------------------------
// 메시지 훅 — Win32Window의 IWindowMessageHook 체인에 등록. ImGui가 마우스/키보드를
// 먼저 소비하면 true를 반환해 이후 훅·기본 이벤트 발행을 중단시킨다(선점).
// ---------------------------------------------------------------------------
struct DebugUi::HookImpl final : public IWindowMessageHook {
    bool OnMessage(void* hwnd, uint32_t msg, uint64_t wparam, int64_t lparam) override {
        // ImGui 컨텍스트가 아직 없으면 통과 (Initialize 진행 중 방어).
        if (ImGui::GetCurrentContext() == nullptr) return false;

        const LRESULT handled = ImGui_ImplWin32_WndProcHandler(
            static_cast<HWND>(hwnd), static_cast<UINT>(msg),
            static_cast<WPARAM>(wparam), static_cast<LPARAM>(lparam));
        if (handled != 0)
            return true;   // ImGui가 소비 → 체인 중단

        // ImGui가 캡처 중인 입력은 게임/에디터로 흘리지 않는다(마우스 클릭 관통 방지).
        const ImGuiIO& io = ImGui::GetIO();
        switch (msg) {
        case WM_MOUSEMOVE: case WM_LBUTTONDOWN: case WM_LBUTTONUP:
        case WM_RBUTTONDOWN: case WM_RBUTTONUP: case WM_MBUTTONDOWN:
        case WM_MBUTTONUP: case WM_MOUSEWHEEL: case WM_MOUSEHWHEEL:
            if (io.WantCaptureMouse) return true;
            break;
        case WM_KEYDOWN: case WM_KEYUP: case WM_SYSKEYDOWN:
        case WM_SYSKEYUP: case WM_CHAR:
            if (io.WantCaptureKeyboard) return true;
            break;
        default:
            break;
        }
        return false;
    }
};

DebugUi::~DebugUi() {
    Shutdown();
}

Expected<void, Error> DebugUi::Initialize(win32::Win32Window& window, rhi::IDevice& device,
                                          int messageHookPriority) {
    if (m_initialized)
        return Error{"DebugUi::Initialize — already initialized", 1};

    void* hwnd = window.GetNativeHandle();
    if (hwnd == nullptr)
        return Error{"DebugUi::Initialize — window has no native HWND", 1};

    auto* d3dDevice  = static_cast<ID3D11Device*>(device.GetNativeDevice());
    auto* d3dContext = static_cast<ID3D11DeviceContext*>(device.GetNativeContext());
    if (d3dDevice == nullptr || d3dContext == nullptr)
        return Error{"DebugUi::Initialize — device native D3D11 handles unavailable", 2};

    IMGUI_CHECKVERSION();
    if (ImGui::CreateContext() == nullptr)
        return Error{"DebugUi::Initialize — ImGui::CreateContext failed", 3};

    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;      // 도킹 활성 (docs/07)
    io.IniFilename = nullptr;                              // imgui.ini 파일 미생성 (엔진이 레이아웃 관리)
    ApplySkin();                                           // devTool 스킨(다크+주황 액센트+라운드)

    // CJK 폰트(HaFont) 로드 — 한글/중/일 렌더링. 못 찾으면 기본 폰트 유지(no-op).
    LoadEditorFonts(18.0f);

    if (!ImGui_ImplWin32_Init(hwnd)) {
        ImGui::DestroyContext();
        return Error{"DebugUi::Initialize — ImGui_ImplWin32_Init failed", 4};
    }
    if (!ImGui_ImplDX11_Init(d3dDevice, d3dContext)) {
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext();
        return Error{"DebugUi::Initialize — ImGui_ImplDX11_Init failed", 5};
    }

    // 메시지 훅 등록 — 낮은 priority = 먼저(선점). 입력 계층보다 앞서 ImGui가 소비.
    m_hook = new HookImpl();
    window.AddMessageHook(m_hook, messageHookPriority);

    m_window      = &window;
    m_hwnd        = hwnd;
    m_initialized = true;
    return {};   // 성공 (Expected<void,Error> 기본 생성)
}

void DebugUi::BeginFrame() {
    if (!m_initialized) return;
    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();
}

void DebugUi::EndFrame() {
    if (!m_initialized) return;
    ImGui::Render();
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

    // 입력 선점 주입: WantCapture는 이 프레임 위젯 처리 후(=Render 시점) 확정된다.
    // 여기서 InputState에 억제 플래그를 넣으면 다음 프레임 PumpMessages(WM_INPUT)가
    // 게이팅되어 Raw Input 상태 갱신이 막힌다(UI 타이핑 중 캐릭터 이동 방지).
    if (m_input) {
        const ImGuiIO& io = ImGui::GetIO();
        m_input->SetKeyboardSuppressed(io.WantCaptureKeyboard);
        m_input->SetMouseSuppressed(io.WantCaptureMouse);
    }
}

void DebugUi::Shutdown() {
    if (!m_initialized) {
        // Initialize가 중간 실패한 경우에도 훅 포인터가 남아있을 수 있으니 정리.
        delete m_hook;
        m_hook = nullptr;
        return;
    }
    // 억제 플래그를 해제해 InputState가 억제 상태로 남지 않게 한다.
    if (m_input) {
        m_input->SetKeyboardSuppressed(false);
        m_input->SetMouseSuppressed(false);
    }
    if (m_window && m_hook)
        m_window->RemoveMessageHook(m_hook);

    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();

    delete m_hook;
    m_hook        = nullptr;
    m_window      = nullptr;
    m_hwnd        = nullptr;
    m_initialized = false;
}

} // namespace mye::imgui
