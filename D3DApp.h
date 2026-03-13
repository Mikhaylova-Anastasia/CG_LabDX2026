// D3DApp.h
#pragma once
#include "Common.h"
#include "Timer.h"
#include "InputDevice.h"

class D3DApp
{
public:
    D3DApp(HINSTANCE hInstance);
    virtual ~D3DApp();

    int Run();

    virtual bool Initialize();

    
    virtual void OnResize();
    virtual void Update(const GameTimer& gt) = 0;
    virtual void Draw(const GameTimer& gt) = 0;

    HWND GetHwnd() const { return m_hWnd; }

protected:
    bool InitWindow();
    bool InitDirect3D();

    void CreateRtvAndDsvDescriptorHeaps();
    void CreateSwapChain();
    void CreateDepthStencilBuffer();
    void FlushCommandQueue();

    void CalculateFrameStats(); 

protected:
    static D3DApp* mApp; 

    HINSTANCE m_hInstance = nullptr;
    HWND      m_hWnd = nullptr;

    UINT mClientWidth = 1280;
    UINT mClientHeight = 720;

    GameTimer mTimer;

    
    InputDevice mInput;

    
    std::wstring mMainWndCaption = L"Dx12 Cube";

    
    int   mFrameCount = 0;
    float mTimeElapsed = 0.0f;

    
    ComPtr<IDXGIFactory4>       mDxgiFactory;
    ComPtr<ID3D12Device>        mDevice;
    ComPtr<ID3D12Fence>         mFence;
    UINT64                      mCurrentFence = 0;

    ComPtr<ID3D12CommandQueue>      mCommandQueue;
    ComPtr<ID3D12CommandAllocator>  mCommandAllocator;
    ComPtr<ID3D12GraphicsCommandList> mCommandList;

    static const int SwapChainBufferCount = 2;
    ComPtr<IDXGISwapChain>      mSwapChain;
    int                         mCurrBackBuffer = 0;
    ComPtr<ID3D12Resource>      mSwapChainBuffer[SwapChainBufferCount];
    ComPtr<ID3D12Resource>      mDepthStencilBuffer;

    ComPtr<ID3D12DescriptorHeap> mRtvHeap;
    ComPtr<ID3D12DescriptorHeap> mDsvHeap;

    UINT mRtvDescriptorSize = 0;
    UINT mDsvDescriptorSize = 0;
    UINT mCbvSrvUavDescriptorSize = 0;

    D3D12_VIEWPORT mScreenViewport;
    D3D12_RECT     mScissorRect;

protected:
    ID3D12Resource* CurrentBackBuffer() const;
    D3D12_CPU_DESCRIPTOR_HANDLE CurrentBackBufferView() const;
    D3D12_CPU_DESCRIPTOR_HANDLE DepthStencilView() const;

    virtual LRESULT MsgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

    friend LRESULT CALLBACK MainWndProc(HWND, UINT, WPARAM, LPARAM);
};
