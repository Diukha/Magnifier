#pragma once
#include <windows.h>
#include <roapi.h>
#include <winstring.h>
// This MinGW/WIDL release emits duplicate IReference<BYTE>/IReference<boolean>
// C++ specializations. Neither specialization is used by our capture interfaces.
#ifdef __MINGW32__
#define ____FIReference_1_boolean_INTERFACE_DEFINED__
#endif
#include <windows.graphics.capture.h>
#include <windows.graphics.capture.interop.h>
#include <windows.graphics.directx.direct3d11.interop.h>
#include <d3d11_4.h>
#include <wrl/client.h>
#include <memory>
#include <atomic>

namespace lens {
namespace WGC = ABI::Windows::Graphics::Capture;
namespace WGD = ABI::Windows::Graphics::DirectX::Direct3D11;
using Microsoft::WRL::ComPtr;

class Capture {
    struct Event {
        HANDLE handle=CreateEventW(nullptr,FALSE,FALSE,nullptr);
        ~Event() { if(handle) CloseHandle(handle); }
    };
    using Handler=ABI::Windows::Foundation::ITypedEventHandler<WGC::Direct3D11CaptureFramePool*,IInspectable*>;
    class ArrivedHandler final : public Handler {
        std::atomic<ULONG> references_{1};
        std::shared_ptr<Event> event_;
    public:
        explicit ArrivedHandler(std::shared_ptr<Event> event):event_(std::move(event)) {}
        HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid,void** result) override {
            if(!result) return E_POINTER;
            *result=nullptr;
            if(iid==__uuidof(IUnknown) || iid==__uuidof(Handler) || iid==__uuidof(IAgileObject)) {
                *result=static_cast<Handler*>(this);AddRef();return S_OK;
            }
            return E_NOINTERFACE;
        }
        ULONG STDMETHODCALLTYPE AddRef() override { return ++references_; }
        ULONG STDMETHODCALLTYPE Release() override {
            ULONG remaining=--references_;
            if(!remaining) delete this;
            return remaining;
        }
        HRESULT STDMETHODCALLTYPE Invoke(WGC::IDirect3D11CaptureFramePool*,IInspectable*) override {
            SetEvent(event_->handle);return S_OK;
        }
    };
    std::shared_ptr<Event> arrived_;
    ComPtr<WGC::IGraphicsCaptureItem> item_;
    ComPtr<WGD::IDirect3DDevice> winrtDevice_;
    ComPtr<WGC::IDirect3D11CaptureFramePool> pool_;
    ComPtr<WGC::IGraphicsCaptureSession> session_;
    ComPtr<WGC::IGraphicsCaptureSession2> cursorSession_;
    EventRegistrationToken token_{};
    bool subscribed_=false,ownsRuntime_=false;
    ABI::Windows::Graphics::SizeInt32 size_{};

    template<class T> static HRESULT factory(const wchar_t* name,ComPtr<T>& result) {
        HSTRING text=nullptr;
        HRESULT hr=WindowsCreateString(name,lstrlenW(name),&text);
        if(SUCCEEDED(hr)) hr=RoGetActivationFactory(text,IID_PPV_ARGS(result.GetAddressOf()));
        WindowsDeleteString(text);
        return hr;
    }
    template<class T> static void close(const ComPtr<T>& object) {
        ComPtr<ABI::Windows::Foundation::IClosable> closable;
        if(object && SUCCEEDED(object.As(&closable))) closable->Close();
    }
public:
    ~Capture() { stop(); if(ownsRuntime_) RoUninitialize(); }
    void stop() {
        if(pool_ && subscribed_) pool_->remove_FrameArrived(token_);
        subscribed_=false;
        close(session_);close(pool_);
        cursorSession_.Reset();session_.Reset();pool_.Reset();
        item_.Reset();winrtDevice_.Reset();arrived_.reset();
    }
    HRESULT start(ID3D11Device* device,ID3D11DeviceContext* context,HMONITOR monitor) {
        stop();
        if(!ownsRuntime_) {
            HRESULT hr=RoInitialize(RO_INIT_MULTITHREADED);
            if(FAILED(hr) && hr!=RPC_E_CHANGED_MODE) return hr;
            ownsRuntime_=SUCCEEDED(hr);
        }
        ComPtr<ID3D11Multithread> multithread;
        if(SUCCEEDED(context->QueryInterface(IID_PPV_ARGS(multithread.GetAddressOf()))))
            multithread->SetMultithreadProtected(TRUE);
        ComPtr<IDXGIDevice> dxgiDevice;
        HRESULT hr=device->QueryInterface(IID_PPV_ARGS(dxgiDevice.GetAddressOf()));
        if(FAILED(hr)) return hr;
        ComPtr<IInspectable> inspectable;
        hr=CreateDirect3D11DeviceFromDXGIDevice(dxgiDevice.Get(),&inspectable);
        if(FAILED(hr) || FAILED(hr=inspectable.As(&winrtDevice_))) return hr;
        ComPtr<IGraphicsCaptureItemInterop> interop;
        hr=factory(L"Windows.Graphics.Capture.GraphicsCaptureItem",interop);
        if(FAILED(hr)) return hr;
        hr=interop->CreateForMonitor(monitor,IID_PPV_ARGS(item_.GetAddressOf()));
        if(FAILED(hr) || FAILED(hr=item_->get_Size(&size_))) return hr;
        ComPtr<WGC::IDirect3D11CaptureFramePoolStatics2> poolFactory;
        hr=factory(L"Windows.Graphics.Capture.Direct3D11CaptureFramePool",poolFactory);
        if(FAILED(hr)) return hr;
        hr=poolFactory->CreateFreeThreaded(winrtDevice_.Get(),
            ABI::Windows::Graphics::DirectX::DirectXPixelFormat_B8G8R8A8UIntNormalized,2,size_,&pool_);
        if(FAILED(hr) || FAILED(hr=pool_->CreateCaptureSession(item_.Get(),&session_))) return hr;
        // Mandatory, before StartCapture: unlike DXGI, WGC guarantees a
        // cursor-free image even when the pointer would otherwise be baked in.
        if(FAILED(hr=session_.As(&cursorSession_)) ||
           FAILED(hr=cursorSession_->put_IsCursorCaptureEnabled(false))) return hr;
        boolean enabled=true;
        if(FAILED(hr=cursorSession_->get_IsCursorCaptureEnabled(&enabled))) return hr;
        if(enabled) return E_FAIL;
        // Best effort using the documented setting. Windows may retain its
        // capture indicator if borderless capture has not been permitted.
        ComPtr<WGC::IGraphicsCaptureSession3> borderSession;
        if(SUCCEEDED(session_.As(&borderSession))) borderSession->put_IsBorderRequired(false);
        arrived_=std::make_shared<Event>();
        if(!arrived_->handle) return HRESULT_FROM_WIN32(GetLastError());
        ComPtr<Handler> handler;
        handler.Attach(new ArrivedHandler(arrived_));
        hr=pool_->add_FrameArrived(handler.Get(),&token_);
        if(FAILED(hr)) return hr;
        subscribed_=true;
        return session_->StartCapture();
    }
    HRESULT next(bool first,ComPtr<WGC::IDirect3D11CaptureFrame>& latest,ComPtr<ID3D11Texture2D>& texture) {
        latest.Reset();texture.Reset();
        if(!pool_) return E_UNEXPECTED;
        for(int attempt=0;attempt<(first?2:1);++attempt) {
            // Drain to the newest complete frame; never queue stale desktop
            // images behind the user's current cursor coordinates.
            for(int i=0;i<3;++i) {
                ComPtr<WGC::IDirect3D11CaptureFrame> candidate;
                HRESULT hr=pool_->TryGetNextFrame(&candidate);
                if(FAILED(hr)) { close(latest);return hr; }
                if(!candidate) break;
                close(latest);latest=candidate;
            }
            if(latest) break;
            if(first && attempt==0) WaitForSingleObject(arrived_->handle,500);
        }
        if(!latest) return S_FALSE;
        ABI::Windows::Graphics::SizeInt32 actual{};
        HRESULT hr=latest->get_ContentSize(&actual);
        if(FAILED(hr)) { close(latest);return hr; }
        if(actual.Width!=size_.Width || actual.Height!=size_.Height) {
            close(latest);return DXGI_ERROR_ACCESS_LOST;
        }
        ComPtr<WGD::IDirect3DSurface> surface;
        ComPtr<::Windows::Graphics::DirectX::Direct3D11::IDirect3DDxgiInterfaceAccess> access;
        if(FAILED(hr=latest->get_Surface(&surface)) || FAILED(hr=surface.As(&access)) ||
           FAILED(hr=access->GetInterface(IID_PPV_ARGS(texture.GetAddressOf())))) {
            close(latest);return hr;
        }
        return S_OK;
    }
    void release(const ComPtr<WGC::IDirect3D11CaptureFrame>& frame) { close(frame); }
    bool cursorDisabled() const {
        boolean enabled=true;
        return cursorSession_ && SUCCEEDED(cursorSession_->get_IsCursorCaptureEnabled(&enabled)) && !enabled;
    }
#ifdef LENS_TEST_READBACK
    HRESULT setCursorForTest(bool enabled) { return cursorSession_->put_IsCursorCaptureEnabled(enabled); }
#endif
};
}
