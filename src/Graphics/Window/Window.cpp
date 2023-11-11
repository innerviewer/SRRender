//
// Created by Nikita on 18.11.2020.
//

#include <Utils/Math/Vector2.h>
#include <Utils/Types/Thread.h>
#include <Utils/Input/InputSystem.h>
#include <Utils/Platform/Platform.h>
#include <Utils/ResourceManager/ResourceManager.h>

#include <Graphics/Window/Window.h>
#include <Graphics/Types/Camera.h>
#include <Graphics/Types/Material.h>
#include <Graphics/Types/Texture.h>
#include <Graphics/Types/Framebuffer.h>
#include <Graphics/GUI/Editor/Theme.h>
#include <Graphics/GUI/WidgetManager.h>
#include <Graphics/GUI/Widget.h>

namespace SR_GRAPH_NS {
    Window::Window()
        : Super(this)
    { }

    Window::~Window() {
        SRAssert(!m_windowImpl && !m_thread);
    }

    bool Window::Initialize(const std::string& name, const SR_MATH_NS::UVector2& size) {
        SR_INFO("Window::Initialize() : initializing the window...");

        m_windowImpl = BasicWindowImpl::CreatePlatformWindow(BasicWindowImpl::WindowType::Auto);
        if (!m_windowImpl) {
            SR_ERROR("Window::Initialize() : failed to create window implementation!");
            return false;
        }

        m_thread = SR_HTYPES_NS::Thread::Factory::Instance().CreateEmpty();
        m_thread->SetName("Window");

        if (!m_thread->Run([this]() { ThreadFunction(); })) {
            SR_ERROR("Window::Initialize() : failed to run thread!");
            return false;
        }

        return m_thread->Execute([&]() -> bool {
            if (!m_windowImpl->Initialize(name, SR_MATH_NS::IVector2(), size, false, true)) {
                SR_ERROR("Window::Initialize() : failed to initialize window implementation!");
                return false;
            }

            m_windowImpl->SetIcon(SR_UTILS_NS::ResourceManager::Instance().GetResPath().Concat("Engine/Textures/icon.ico"));

            return true;
        });
    }

    void Window::ThreadFunction() {
        SR_INFO("Window::Thread() : running window's thread...");

        if (!m_windowImpl) {
            SR_ERROR("Window::Thread() : window implementation is nullptr!");
            return;
        }

        while (m_windowImpl && !m_windowImpl->IsValid() && !m_windowImpl->IsClosed()) {
            m_thread->Synchronize();
        }

        SR_INFO("Window::Thread() : thread synchronized.");

        double deltaTime = 0;
        uint32_t frames = 0;

        while (m_windowImpl->IsValid()) {
            auto t_start = std::chrono::high_resolution_clock::now();

            PollEvents();

            if (m_drawCallback) {
                m_drawCallback();
            }

            m_thread->Synchronize();

            auto t_end = std::chrono::high_resolution_clock::now();

            const double_t milliseconds = std::chrono::duration<double_t, std::milli>(t_end - t_start).count();
            deltaTime += milliseconds / CLOCKS_PER_SEC;
            ++frames;

            if (deltaTime > 1.0) { /// every second
                m_framesPerSecond = SR_MAX(0, frames - 1);
                /// SR_LOG(SR_UTILS_NS::Format("FPS: %i; ms: %f", m_framesPerSecond, milliseconds));
                frames = 0; deltaTime = 0;
            }
        }

        SR_GRAPH("Window::Thread() : exiting from main cycle.");

        if (m_closeCallback) {
            m_closeCallback();
        }

        SR_INFO("Window::Thread() : stopping window thread...");
    }

    SR_HTYPES_NS::Thread::Ptr Window::GetThread() const {
        return m_thread;
    }

    SR_MATH_NS::UVector2 Window::GetSize() const {
        if (!m_windowImpl) {
            SR_ERROR("Window::GetSize() : window implementation is nullptr.");
            return { };
        }

        return SR_MATH_NS::UVector2(m_windowImpl->GetWidth(), m_windowImpl->GetHeight());
    }

    void Window::SetDrawCallback(const Window::DrawCallback& callback) {
        m_drawCallback = callback;
    }

    bool Window::IsWindowFocus() const {
        return m_windowImpl->IsFocused();
    }

    bool Window::IsWindowCollapsed() const {
        return m_windowImpl->IsCollapsed();
    }

    void Window::SetCloseCallback(const Window::CloseCallback &callback) {
        m_closeCallback = callback;
    }

    void Window::Close() {
        SR_GRAPH_LOG("Window::Close() : closing the window...");

        auto&& cachePath = SR_UTILS_NS::ResourceManager::Instance().GetCachePath();
        auto&& windowSettingsPath = cachePath.Concat("WindowSettings.xml");

        auto&& windowSettings = SR_XML_NS::Document::New();
        auto&& rootNode = windowSettings.Root().AppendNode("Settings");

        if (!GetSize().HasZero()) {
            rootNode.AppendNode("Size").AppendAttribute(GetSize());
        }

        rootNode.AppendNode("Position").AppendAttribute(GetPosition());
        rootNode.AppendAttribute("IsMaximized", IsMaximized());

        windowSettings.Save(windowSettingsPath);

        if (m_windowImpl) {
            m_windowImpl->Close();
        }

        if (m_thread) {
            m_thread->TryJoin();
            m_thread->Free();
            m_thread = nullptr;
        }

        SR_SAFE_DELETE_PTR(m_windowImpl);
    }

    Window::WindowHandle Window::GetHandle() const {
        return m_windowImpl->GetHandle();
    }

    bool Window::IsValid() const {
        if (!m_windowImpl) {
            return false;
        }

        return m_windowImpl->IsValid();
    }

    void Window::SetResizeCallback(const Window::ResizeCallback &callback) {
        m_windowImpl->SetResizeCallback([callback](auto&& pWin, int32_t width, int32_t height) {
            callback(SR_MATH_NS::UVector2(static_cast<uint32_t>(width), static_cast<uint32_t>(height)));
        });
    }

    bool Window::IsFullScreen() const {
        return false;
    }

    void Window::SetFullScreen(bool value) {

    }

    void Window::SetFocusCallback(const Window::FocusCallback &callback) {
        m_windowImpl->SetFocusCallback([callback](auto&& pWin, auto&& focus) {
            callback(focus);
        });
    }

    void Window::SetScrollCallback(const Window::ScrollCallback &callback) {
        m_windowImpl->SetScrollCallback([callback](auto&& pWin, auto&& xOffset, auto&& yOffset) {
            callback(xOffset, yOffset);
        });
    }

    SR_MATH_NS::IVector2 Window::ScreenToClient(const SR_MATH_NS::IVector2& pos) const {
        return m_windowImpl->ScreenToClient(pos);
    }

    SR_MATH_NS::IVector2 Window::ClientToScreen(const SR_MATH_NS::IVector2& pos) const {
        return m_windowImpl->ClientToScreen(pos);
    }

    SR_MATH_NS::IVector2 Window::GetPosition() const {
        if (!m_windowImpl) {
            SR_ERROR("Window::GetPosition() : window implementation is nullptr.");
            return { };
        }

        return m_windowImpl->GetPosition();
    }

    bool Window::IsMaximized() const {
        if (!m_windowImpl) {
            SR_ERROR("Window::IsMaximized() : window implementation is nullptr.");
            return false;
        }

        if (m_windowImpl->GetState() == WindowState::Maximized) {
            return true;
        }

        SR_NOOP;

        return false;
    }

    bool Window::IsVisible() const {
        if (!m_windowImpl) {
            SR_ERROR("Window::IsVisible() : window implementation is nullptr.");
            return false;
        }

        return m_windowImpl->IsVisible();
    }

    void Window::PollEvents() {
        if (!m_windowImpl) {
            SR_ERROR("Window::PollEvents() : window implementation is nullptr.");
            return;
        }
        m_windowImpl->PollEvents();
    }
}