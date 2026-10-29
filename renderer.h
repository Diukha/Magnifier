#pragma once
#include <windows.h>
#include <d3d11.h>
#include <dxgi1_3.h>
#include <dcomp.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <cstdint>
#include <vector>
#include "geometry.h"
#include "color.h"
#include "capture.h"

namespace lens {
using Microsoft::WRL::ComPtr;
constexpr int kBorder = 3;
inline std::uint32_t premultiplied(Color color) {
    const auto c = [=](unsigned v) { return (v*color.a+127)/255; };
    return (std::uint32_t(color.a)<<24)|(c(color.r)<<16)|(c(color.g)<<8)|c(color.b);
}

// A fixed transparent monitor-sized surface. Lens position and source region
// are shader constants in the SAME GPU frame. No CPU image processing/readback.
class Renderer {
    ComPtr<ID3D11Device> device_;
    ComPtr<ID3D11DeviceContext> context_;
    Capture capture_;
    ComPtr<ID3D11Texture2D> desktop_;
    ComPtr<ID3D11ShaderResourceView> desktopView_;
    ComPtr<IDXGISwapChain1> swap_;
    ComPtr<ID3D11RenderTargetView> targetView_;
    ComPtr<IDCompositionDevice> composition_;
    ComPtr<IDCompositionTarget> compositionTarget_;
    ComPtr<IDCompositionVisual> visual_;
    ComPtr<ID3D11VertexShader> vertexShader_;
    ComPtr<ID3D11PixelShader> pixelShader_;
    ComPtr<ID3D11Buffer> constants_;
    ComPtr<ID3D11SamplerState> sampler_;
    HANDLE ready_ = nullptr;
    HMONITOR monitor_ = nullptr;
    RECT bounds_{};
    DXGI_MODE_ROTATION rotation_ = DXGI_MODE_ROTATION_IDENTITY;
    bool hasDesktop_ = false;
    HRESULT error_ = S_OK;
    struct Constants { float lens[4], source[4], color[4], display[4], options[4]; };

    bool check(HRESULT result) { error_=result; return SUCCEEDED(result); }
    void reset() {
        if (context_) context_->ClearState();
        visual_.Reset(); compositionTarget_.Reset(); composition_.Reset();
        targetView_.Reset(); swap_.Reset();
        if (ready_) { CloseHandle(ready_); ready_=nullptr; }
        constants_.Reset(); sampler_.Reset(); pixelShader_.Reset(); vertexShader_.Reset();
        capture_.stop();
        desktopView_.Reset(); desktop_.Reset();
        context_.Reset(); device_.Reset(); monitor_=nullptr; hasDesktop_=false;
#ifdef LENS_TEST_READBACK
        lastFrame_.Reset();
#endif
    }
    bool initialize(HWND window, HMONITOR monitor) {
        reset();
        ComPtr<IDXGIFactory2> factory;
        if (!check(CreateDXGIFactory1(IID_PPV_ARGS(factory.GetAddressOf())))) return false;
        ComPtr<IDXGIAdapter1> selectedAdapter;
        ComPtr<IDXGIOutput1> selectedOutput;
        DXGI_OUTPUT_DESC outputDesc{};
        for (UINT ai=0; !selectedOutput; ++ai) {
            ComPtr<IDXGIAdapter1> adapter;
            HRESULT found=factory->EnumAdapters1(ai,&adapter);
            if (found==DXGI_ERROR_NOT_FOUND) break;
            if (!check(found)) return false;
            for (UINT oi=0; ; ++oi) {
                ComPtr<IDXGIOutput> output;
                found=adapter->EnumOutputs(oi,&output);
                if (found==DXGI_ERROR_NOT_FOUND) break;
                if (!check(found)) return false;
                DXGI_OUTPUT_DESC desc{};
                if (SUCCEEDED(output->GetDesc(&desc)) && desc.Monitor==monitor) {
                    if (!check(output.As(&selectedOutput))) return false;
                    selectedAdapter=adapter; outputDesc=desc; break;
                }
            }
        }
        if (!selectedOutput) { error_=DXGI_ERROR_NOT_FOUND; return false; }
        bounds_=outputDesc.DesktopCoordinates;
        const UINT width=bounds_.right-bounds_.left,height=bounds_.bottom-bounds_.top;
        if (!check(D3D11CreateDevice(selectedAdapter.Get(),D3D_DRIVER_TYPE_UNKNOWN,nullptr,
                   D3D11_CREATE_DEVICE_BGRA_SUPPORT,nullptr,0,D3D11_SDK_VERSION,
                   &device_,nullptr,&context_))) return false;
        if (!check(capture_.start(device_.Get(),context_.Get(),monitor))) return false;
        // WGC monitor frames already use desktop orientation.
        rotation_=DXGI_MODE_ROTATION_IDENTITY;

        DXGI_SWAP_CHAIN_DESC1 desc{};
        desc.Width=width; desc.Height=height; desc.Format=DXGI_FORMAT_B8G8R8A8_UNORM;
        desc.SampleDesc.Count=1; desc.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT;
        desc.BufferCount=2; desc.Scaling=DXGI_SCALING_STRETCH;
        desc.SwapEffect=DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
        desc.AlphaMode=DXGI_ALPHA_MODE_PREMULTIPLIED;
        desc.Flags=DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT;
        if (!check(factory->CreateSwapChainForComposition(device_.Get(),&desc,nullptr,&swap_))) return false;
        ComPtr<IDXGISwapChain2> swap2;
        if (!check(swap_.As(&swap2)) || !check(swap2->SetMaximumFrameLatency(1))) return false;
        ready_=swap2->GetFrameLatencyWaitableObject();
        if (!ready_) { error_=E_FAIL; return false; }
        ComPtr<ID3D11Texture2D> buffer;
        if (!check(swap_->GetBuffer(0,IID_PPV_ARGS(buffer.GetAddressOf()))) ||
            !check(device_->CreateRenderTargetView(buffer.Get(),nullptr,&targetView_))) return false;

        ComPtr<IDXGIDevice> dxgiDevice;
        if (!check(device_.As(&dxgiDevice)) ||
            !check(DCompositionCreateDevice(dxgiDevice.Get(),IID_PPV_ARGS(composition_.GetAddressOf()))) ||
            !check(composition_->CreateTargetForHwnd(window,TRUE,&compositionTarget_)) ||
            !check(composition_->CreateVisual(&visual_)) ||
            !check(visual_->SetContent(swap_.Get())) ||
            !check(compositionTarget_->SetRoot(visual_.Get())) ||
            !check(composition_->Commit())) return false;

        static const char shader[] = R"(
cbuffer State : register(b0) {
    float4 lensRect; float4 sourceRect; float4 borderColor; float4 display; float4 options;
};
Texture2D desktopImage : register(t0);
SamplerState linearSampler : register(s0);
float4 VS(uint id : SV_VertexID) : SV_POSITION {
    float2 uv=float2((id<<1)&2,id&2);
    return float4(uv*float2(2,-2)+float2(-1,1),0,1);
}
float4 PS(float4 position : SV_POSITION) : SV_TARGET {
    float2 p=position.xy-lensRect.xy;
    if (any(p<0) || any(p>=lensRect.zw)) return float4(0,0,0,0);
    float border=options.x;
    if (any(p<border) || any(p>=lensRect.zw-border)) return borderColor;
    float2 uv=(sourceRect.xy+(p-border)/(lensRect.zw-2*border)*sourceRect.zw)/display.xy;
    if (options.y==2) uv=float2(uv.y,1-uv.x);
    else if (options.y==3) uv=1-uv;
    else if (options.y==4) uv=float2(1-uv.y,uv.x);
    return float4(desktopImage.SampleLevel(linearSampler,uv,0).rgb,1);
})";
        ComPtr<ID3DBlob> vs,ps,errors;
        if (!check(D3DCompile(shader,sizeof(shader)-1,nullptr,nullptr,nullptr,"VS","vs_4_0",
                             D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&vs,&errors)) ||
            !check(D3DCompile(shader,sizeof(shader)-1,nullptr,nullptr,nullptr,"PS","ps_4_0",
                             D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&ps,&errors)) ||
            !check(device_->CreateVertexShader(vs->GetBufferPointer(),vs->GetBufferSize(),nullptr,&vertexShader_)) ||
            !check(device_->CreatePixelShader(ps->GetBufferPointer(),ps->GetBufferSize(),nullptr,&pixelShader_))) return false;
        D3D11_BUFFER_DESC cb{}; cb.ByteWidth=sizeof(Constants); cb.Usage=D3D11_USAGE_DYNAMIC;
        cb.BindFlags=D3D11_BIND_CONSTANT_BUFFER; cb.CPUAccessFlags=D3D11_CPU_ACCESS_WRITE;
        if (!check(device_->CreateBuffer(&cb,nullptr,&constants_))) return false;
        D3D11_SAMPLER_DESC sd{}; sd.Filter=D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        sd.AddressU=sd.AddressV=sd.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP; sd.MaxLOD=D3D11_FLOAT32_MAX;
        if (!check(device_->CreateSamplerState(&sd,&sampler_))) return false;
        SetWindowPos(window,HWND_TOPMOST,bounds_.left,bounds_.top,width,height,SWP_NOACTIVATE);
        monitor_=monitor;
        return true;
    }
    bool acquire() {
        // Do not wait for desktop changes in the input thread. A cached GPU
        // texture can be magnified at new cursor coordinates even when static.
        ComPtr<WGC::IDirect3D11CaptureFrame> capturedFrame;
        ComPtr<ID3D11Texture2D> frame;
        HRESULT result=capture_.next(!hasDesktop_,capturedFrame,frame);
        if (result==S_FALSE) return hasDesktop_;
        if (FAILED(result)) { error_=result; reset(); return false; }
        bool ok=true;
        if (frame) {
            if (!desktop_) {
                D3D11_TEXTURE2D_DESC td{}; frame->GetDesc(&td);
                td.Usage=D3D11_USAGE_DEFAULT; td.BindFlags=D3D11_BIND_SHADER_RESOURCE;
                td.CPUAccessFlags=0; td.MiscFlags=0;
                ok=check(device_->CreateTexture2D(&td,nullptr,&desktop_)) &&
                   check(device_->CreateShaderResourceView(desktop_.Get(),nullptr,&desktopView_));
            }
            if (ok) {
                ID3D11ShaderResourceView* empty=nullptr;
                context_->PSSetShaderResources(0,1,&empty);
                context_->CopyResource(desktop_.Get(),frame.Get());
                hasDesktop_=true;
            }
        }
        capture_.release(capturedFrame);
        return ok && hasDesktop_;
    }

public:
    Renderer() = default;
    Renderer(const Renderer&)=delete;
    Renderer& operator=(const Renderer&)=delete;
    ~Renderer() { reset(); }
    HANDLE frameReady() const { return ready_; }
    HRESULT error() const { return error_; }
    bool cursorDisabled() const { return capture_.cursorDisabled(); }
    RECT monitorBounds() const { return bounds_; }
#ifdef LENS_PROFILE
    double captureMs=0,scaleMs=0,alphaMs=0,presentMs=0;
#endif
    bool render(HWND window, Rect destination, Rect source, Color border) {
        RECT dr{destination.x,destination.y,destination.x+destination.w,destination.y+destination.h};
        HMONITOR monitor=MonitorFromRect(&dr,MONITOR_DEFAULTTONEAREST);
        if (!device_ || monitor_!=monitor) if (!initialize(window,monitor)) return false;
#ifdef LENS_PROFILE
        LARGE_INTEGER start{},captured{},done{},frequency{};
        QueryPerformanceFrequency(&frequency); QueryPerformanceCounter(&start);
#endif
        if (!acquire()) return false;
#ifdef LENS_PROFILE
        QueryPerformanceCounter(&captured);
#endif
        Constants data{};
        data.lens[0]=float(destination.x-bounds_.left); data.lens[1]=float(destination.y-bounds_.top);
        data.lens[2]=float(destination.w); data.lens[3]=float(destination.h);
        data.source[0]=float(source.x-bounds_.left); data.source[1]=float(source.y-bounds_.top);
        data.source[2]=float(source.w); data.source[3]=float(source.h);
        const float a=border.a/255.0f;
        data.color[0]=border.r/255.0f*a; data.color[1]=border.g/255.0f*a;
        data.color[2]=border.b/255.0f*a; data.color[3]=a;
        data.display[0]=float(bounds_.right-bounds_.left); data.display[1]=float(bounds_.bottom-bounds_.top);
        data.options[0]=float(kBorder); data.options[1]=float(rotation_);
        D3D11_MAPPED_SUBRESOURCE mapped{};
        if (!check(context_->Map(constants_.Get(),0,D3D11_MAP_WRITE_DISCARD,0,&mapped))) return false;
        *static_cast<Constants*>(mapped.pData)=data;
        context_->Unmap(constants_.Get(),0);
        context_->OMSetRenderTargets(1,targetView_.GetAddressOf(),nullptr);
        D3D11_VIEWPORT viewport{0,0,data.display[0],data.display[1],0,1};
        context_->RSSetViewports(1,&viewport);
        context_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        context_->VSSetShader(vertexShader_.Get(),nullptr,0);
        context_->PSSetShader(pixelShader_.Get(),nullptr,0);
        context_->PSSetConstantBuffers(0,1,constants_.GetAddressOf());
        context_->PSSetShaderResources(0,1,desktopView_.GetAddressOf());
        context_->PSSetSamplers(0,1,sampler_.GetAddressOf());
        context_->Draw(3,0);
#ifdef LENS_TEST_READBACK
        // Test only. Production never copies GPU images back to CPU memory.
        ComPtr<ID3D11Texture2D> presented;
        swap_->GetBuffer(0,IID_PPV_ARGS(presented.GetAddressOf()));
        if (presented) {
            if (!lastFrame_) {
                D3D11_TEXTURE2D_DESC td{}; presented->GetDesc(&td);
                td.BindFlags=0; td.MiscFlags=0;
                device_->CreateTexture2D(&td,nullptr,&lastFrame_);
            }
            if (lastFrame_) context_->CopyResource(lastFrame_.Get(),presented.Get());
        }
#endif
        // Vsync + a one-frame queue pace rendering at the display refresh rate,
        // rather than burning CPU/GPU on hundreds of immediately dropped frames.
        HRESULT hr=swap_->Present(1,0);
#ifdef LENS_PROFILE
        QueryPerformanceCounter(&done);
        double ms=1000.0/frequency.QuadPart;
        captureMs=(captured.QuadPart-start.QuadPart)*ms;
        scaleMs=(done.QuadPart-captured.QuadPart)*ms;
        alphaMs=presentMs=0;
#endif
        if (FAILED(hr)) { error_=hr; reset(); return false; }
        return true;
    }

#ifdef LENS_TEST_READBACK
private:
    ComPtr<ID3D11Texture2D> lastFrame_;
    bool read(ID3D11Texture2D* texture, Rect area, std::vector<std::uint32_t>& pixels) {
        if (!texture) return false;
        D3D11_TEXTURE2D_DESC td{};texture->GetDesc(&td);
        td.Width=area.w;td.Height=area.h;td.Usage=D3D11_USAGE_STAGING;
        td.BindFlags=0;td.CPUAccessFlags=D3D11_CPU_ACCESS_READ;td.MiscFlags=0;
        ComPtr<ID3D11Texture2D> staging;
        if (!check(device_->CreateTexture2D(&td,nullptr,&staging))) return false;
        D3D11_BOX box{UINT(area.x),UINT(area.y),0,UINT(area.x+area.w),UINT(area.y+area.h),1};
        context_->CopySubresourceRegion(staging.Get(),0,0,0,0,texture,0,&box);
        D3D11_MAPPED_SUBRESOURCE mapped{};
        if (!check(context_->Map(staging.Get(),0,D3D11_MAP_READ,0,&mapped))) return false;
        pixels.resize(area.w*area.h);
        for(int row=0;row<area.h;++row) {
            auto* src=reinterpret_cast<const std::uint32_t*>(static_cast<const char*>(mapped.pData)+row*mapped.RowPitch);
            std::copy_n(src,area.w,pixels.data()+row*area.w);
        }
        context_->Unmap(staging.Get(),0);
        return true;
    }
public:
    HRESULT setCursorForTest(bool enabled) { return capture_.setCursorForTest(enabled); }
    bool readSource(Rect area,std::vector<std::uint32_t>& pixels) {
        if(rotation_!=DXGI_MODE_ROTATION_IDENTITY) return false;
        area.x-=bounds_.left;area.y-=bounds_.top;
        return read(desktop_.Get(),area,pixels);
    }
    bool readComposed(Rect area,std::vector<std::uint32_t>& pixels) {
        area.x-=bounds_.left;area.y-=bounds_.top;
        return read(lastFrame_.Get(),area,pixels);
    }
#endif
};
}
