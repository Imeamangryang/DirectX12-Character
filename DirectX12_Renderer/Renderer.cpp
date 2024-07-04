#include "Renderer.h";

const int gNumFrameResources = 3;

Renderer::Renderer(HINSTANCE hInstance) : Window(hInstance)
{
}

Renderer::~Renderer()
{
    if (md3dDevice != nullptr) {
        FlushCommandQueue();
    }
}

bool Renderer::Initialize()
{
    // Window & Direct3D 초기화
    if (!Window::Initialize()) return false;

    // Commandlist 초기화
    ThrowIfFailed(mCommandList->Reset(mDirectCmdListAlloc.Get(), nullptr));

    mCbvSrvDescriptorSize = md3dDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    //LoadCharacters();
    LoadTextures();
    BuildDescriptorHeaps();

    // ImGui 초기화에 수정된 핸들을 사용합니다.
    ImGui_ImplDX12_Init(
        md3dDevice.Get(),
        gNumFrameResources,
        DXGI_FORMAT_R8G8B8A8_UNORM,
        mSrvDescriptorHeap.Get(),
        CD3DX12_CPU_DESCRIPTOR_HANDLE(mSrvDescriptorHeap->GetCPUDescriptorHandleForHeapStart(), 1, mCbvSrvDescriptorSize),
        CD3DX12_GPU_DESCRIPTOR_HANDLE(mSrvDescriptorHeap->GetGPUDescriptorHandleForHeapStart(), 1, mCbvSrvDescriptorSize)
    );

    // ========================================================================================================
    // Root Signature 구성
    // ========================================================================================================
    CD3DX12_DESCRIPTOR_RANGE texTable[2];
    texTable[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0); // t0 : Texture
    texTable[1].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 1); // imgui 리소스 용

    CD3DX12_ROOT_PARAMETER slotRootParameter[4];
    slotRootParameter[0].InitAsDescriptorTable(_countof(texTable), texTable, D3D12_SHADER_VISIBILITY_PIXEL); 
    slotRootParameter[1].InitAsConstantBufferView(0); // b0 : ObjectCB
    slotRootParameter[2].InitAsConstantBufferView(1); // b1 : PassCB
    slotRootParameter[3].InitAsConstantBufferView(2); // b2 : MaterialCB

    auto staticSamplers = GetStaticSamplers(); // Static Sampler

    CD3DX12_ROOT_SIGNATURE_DESC rootSigDesc(4, slotRootParameter, (UINT)staticSamplers.size(), staticSamplers.data(), D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

    HRESULT hr = D3D12SerializeRootSignature(&rootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1, serializedRootSig.GetAddressOf(), errorBlob.GetAddressOf());

    if (errorBlob != nullptr)
    {
        ::OutputDebugStringA(static_cast<char*>(errorBlob->GetBufferPointer()));
    }
    ThrowIfFailed(hr);

    ThrowIfFailed(md3dDevice->CreateRootSignature(0, serializedRootSig->GetBufferPointer(), serializedRootSig->GetBufferSize(), IID_PPV_ARGS(mRootSignature.GetAddressOf())));

    // Shader Complie
    mVertexShader = d3dUtil::CompileShader(L"VertexShader.hlsl", nullptr, "VS", "vs_5_0");
    mPixelShader = d3dUtil::CompileShader(L"PixelShader.hlsl", nullptr, "PS", "ps_5_0");

    // InputLayout 구성
    mInputLayout =
    {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    };

    // Pipeline State Object 생성
    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
    ZeroMemory(&psoDesc, sizeof(D3D12_GRAPHICS_PIPELINE_STATE_DESC));
    psoDesc.InputLayout = { mInputLayout.data(), static_cast<UINT>(mInputLayout.size()) };
    psoDesc.pRootSignature = mRootSignature.Get();
    psoDesc.VS =
    {
        reinterpret_cast<BYTE*>(mVertexShader->GetBufferPointer()),
        mVertexShader->GetBufferSize()
    };
    psoDesc.PS =
    {
        reinterpret_cast<BYTE*>(mPixelShader->GetBufferPointer()),
        mPixelShader->GetBufferSize()
    };
    psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
    //psoDesc.RasterizerState.FillMode = D3D12_FILL_MODE_WIREFRAME;
    psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
    psoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
    psoDesc.SampleMask = UINT_MAX;
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    psoDesc.NumRenderTargets = 1;
    psoDesc.RTVFormats[0] = mBackBufferFormat;
    psoDesc.SampleDesc.Count = m4xMsaaState ? 4 : 1;
    psoDesc.SampleDesc.Quality = m4xMsaaState ? (m4xMsaaQuality - 1) : 0;
    psoDesc.DSVFormat = mDepthStencilFormat;
    ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&mPSO)));

    // ========================================================================================================
    // 렌더 아이템 구성
    // ========================================================================================================
    BuildBoxGeometry();

    // ========================================================================================================
    // 머티리얼 구성
    // ========================================================================================================
    BuildMaterials();

    // ========================================================================================================
    // 렌더 아이템 구성
    // ========================================================================================================
    BuildRenderItems();

    // ========================================================================================================
    // 프레임 자원 구성
    // ========================================================================================================
    BuildFrameResources();

    // Execute the initialization commands.
    ThrowIfFailed(mCommandList->Close());
    ID3D12CommandList* cmdsLists[] = { mCommandList.Get() };
    mCommandQueue->ExecuteCommandLists(_countof(cmdsLists), cmdsLists);

    // Wait until initialization is complete.
    FlushCommandQueue();

    return true;
}

void Renderer::OnResize()
{
    Window::OnResize();

    mCamera.SetLens(0.25f * MathHelper::Pi, AspectRatio(), 1.0f, 1000.0f);
}

void Renderer::Update(const GameTimer& gt)
{
    OnKeyboardInput(gt);

    // Cycle through the circular frame resource array.
    mCurrFrameResourceIndex = (mCurrFrameResourceIndex + 1) % gNumFrameResources;
    mCurrFrameResource = mFrameResources[mCurrFrameResourceIndex].get();

    // Has the GPU finished processing the commands of the current frame resource?
    // If not, wait until the GPU has completed commands up to this fence point.
    if (mCurrFrameResource->Fence != 0 && mFence->GetCompletedValue() < mCurrFrameResource->Fence)
    {
        HANDLE eventHandle = CreateEventEx(nullptr, false, false, EVENT_ALL_ACCESS);
        ThrowIfFailed(mFence->SetEventOnCompletion(mCurrFrameResource->Fence, eventHandle));
        WaitForSingleObject(eventHandle, INFINITE);
        CloseHandle(eventHandle);
    }

    UpdateObjectCBs(gt);
    UpdateMaterialCBs(gt);
    UpdateMainPassCB(gt);
}

void Renderer::Draw(const GameTimer& gt)
{
    // 명령 레코딩에 연관된 메모리를 재사용합니다.
    // 연관된 명령 목록이 GPU에서 실행을 완료했을 때만 재설정할 수 있습니다.
    auto cmdListAlloc = mCurrFrameResource->CmdListAlloc;
    ThrowIfFailed(cmdListAlloc->Reset());

    // A command list can be reset after it has been added to the command queue via ExecuteCommandList.
    // Reusing the command list reuses memory.
    ThrowIfFailed(mCommandList->Reset(mDirectCmdListAlloc.Get(), mPSO.Get()));

    // Viewport 영역 설정
    mCommandList->RSSetViewports(1, &mScreenViewport);
    mCommandList->RSSetScissorRects(1, &mScissorRect);

    // 리소스 상태 전이 : Present -> Render Target
    mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(CurrentBackBuffer(),
        D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET));

    // 백 버퍼와 깊이 버퍼를 클리어
    mCommandList->ClearRenderTargetView(CurrentBackBufferView(), Colors::LightSteelBlue, 0, nullptr);
    mCommandList->ClearDepthStencilView(DepthStencilView(), D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f, 0, 0, nullptr);

    // 렌더링할 타겟을 백 버퍼와 깊이 버퍼로 설정
    mCommandList->OMSetRenderTargets(1, &CurrentBackBufferView(), true, &DepthStencilView());

    // Descriptor Heap 설정 : SRV
    ID3D12DescriptorHeap* descriptorHeaps[] = { mSrvDescriptorHeap.Get()};
    mCommandList->SetDescriptorHeaps(_countof(descriptorHeaps), descriptorHeaps);

    // Root Signature 설정
    mCommandList->SetGraphicsRootSignature(mRootSignature.Get());

    // 상수 버퍼 설정 : PassCB
    auto passCB = mCurrFrameResource->PassCB->Resource();
    passCB->SetName(L"Pass Constant Buffer");
    mCommandList->SetGraphicsRootConstantBufferView(2, passCB->GetGPUVirtualAddress());

    UINT objCBByteSize = d3dUtil::CalcConstantBufferByteSize(sizeof(ObjectConstants));
    UINT matCBByteSize = d3dUtil::CalcConstantBufferByteSize(sizeof(MaterialConstants));

    auto objectCB = mCurrFrameResource->ObjectCB->Resource();
    auto matCB = mCurrFrameResource->MaterialCB->Resource();

    // For each render item...
    for (size_t i = 0; i < mOpaqueRitems.size(); ++i)
    {
        auto ri = mOpaqueRitems[i];

        mCommandList->IASetVertexBuffers(0, 1, &ri->Geo->VertexBufferView());
        mCommandList->IASetIndexBuffer(&ri->Geo->IndexBufferView());
        mCommandList->IASetPrimitiveTopology(ri->PrimitiveType);

        CD3DX12_GPU_DESCRIPTOR_HANDLE tex(mSrvDescriptorHeap->GetGPUDescriptorHandleForHeapStart());
        tex.Offset(ri->Mat->DiffuseSrvHeapIndex, mCbvSrvDescriptorSize);

        D3D12_GPU_VIRTUAL_ADDRESS objCBAddress = objectCB->GetGPUVirtualAddress() + ri->ObjCBIndex * objCBByteSize;
        D3D12_GPU_VIRTUAL_ADDRESS matCBAddress = matCB->GetGPUVirtualAddress() + ri->Mat->MatCBIndex * matCBByteSize;

        mCommandList->SetGraphicsRootDescriptorTable(0, tex);
        mCommandList->SetGraphicsRootConstantBufferView(1, objCBAddress);
        mCommandList->SetGraphicsRootConstantBufferView(3, matCBAddress);

        mCommandList->DrawIndexedInstanced(ri->IndexCount, 1, ri->StartIndexLocation, ri->BaseVertexLocation, 0);
    }

    ImGui_ImplDX12_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();
    ImGui::ShowDemoWindow();

    ImGui::Render();
    ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), mCommandList.Get());

    // Indicate a state transition on the resource usage.
    mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(CurrentBackBuffer(),
        D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT));

    // Done recording commands.
    ThrowIfFailed(mCommandList->Close());

    // Add the command list to the queue for execution.
    ID3D12CommandList* cmdsLists[] = { mCommandList.Get() };
    mCommandQueue->ExecuteCommandLists(_countof(cmdsLists), cmdsLists);

    // Swap the back and front buffers
    ThrowIfFailed(mSwapChain->Present(0, 0));
    mCurrBackBuffer = (mCurrBackBuffer + 1) % SwapChainBufferCount;

    // Advance the fence value to mark commands up to this fence point.
    mCurrFrameResource->Fence = ++mCurrentFence;

    // Add an instruction to the command queue to set a new fence point. 
    // Because we are on the GPU timeline, the new fence point won't be 
    // set until the GPU finishes processing all the commands prior to this Signal().
    mCommandQueue->Signal(mFence.Get(), mCurrentFence);
}

void Renderer::OnMouseDown(WPARAM btnState, int x, int y)
{
    mLastMousePos.x = x;
    mLastMousePos.y = y;

    SetCapture(mhMainWnd);
}

void Renderer::OnMouseUp(WPARAM btnState, int x, int y)
{
    ReleaseCapture();
}

void Renderer::OnMouseMove(WPARAM btnState, int x, int y)
{
    if ((btnState & MK_LBUTTON) != 0)
    {
        // Make each pixel correspond to a quarter of a degree.
        float dx = XMConvertToRadians(0.25f * static_cast<float>(x - mLastMousePos.x));
        float dy = XMConvertToRadians(0.25f * static_cast<float>(y - mLastMousePos.y));

        mCamera.Pitch(dy);
        mCamera.RotateY(dx);
    }

    mLastMousePos.x = x;
    mLastMousePos.y = y;
}

void Renderer::OnKeyboardInput(const GameTimer& gt)
{
    const float dt = gt.DeltaTime();

    if (GetAsyncKeyState('W') & 0x8000)
        mCamera.Walk(10.0f * dt);

    if (GetAsyncKeyState('S') & 0x8000)
        mCamera.Walk(-10.0f * dt);

    if (GetAsyncKeyState('A') & 0x8000)
        mCamera.Strafe(-10.0f * dt);

    if (GetAsyncKeyState('D') & 0x8000)
        mCamera.Strafe(10.0f * dt);

    if (GetAsyncKeyState('Q') & 0x8000)
        mCamera.UpDown(-10.0f * dt);

    if (GetAsyncKeyState('E') & 0x8000)
        mCamera.UpDown(10.0f * dt);

    mCamera.UpdateViewMatrix();
}

void Renderer::BuildBoxGeometry()
{
    GeometryGenerator geoGen;
    GeometryGenerator::MeshData box = geoGen.CreateBox(1.0f, 1.0f, 1.0f, 1);

    SubmeshGeometry boxSubmesh;
    boxSubmesh.IndexCount = (UINT)box.Indices32.size();
    boxSubmesh.StartIndexLocation = 0;
    boxSubmesh.BaseVertexLocation = 0;

    std::vector<Vertex> vertices(box.Vertices.size());

    for (size_t i = 0; i < box.Vertices.size(); ++i)
    {
        vertices[i].Pos = box.Vertices[i].Position;
        vertices[i].Normal = box.Vertices[i].Normal;
        vertices[i].Tex = box.Vertices[i].TexC;
    }

    std::vector<std::uint16_t> indices = box.GetIndices16();

    const UINT vbByteSize = (UINT)vertices.size() * sizeof(Vertex);
    const UINT ibByteSize = (UINT)indices.size() * sizeof(std::uint16_t);

    auto geo = std::make_unique<MeshGeometry>();
    geo->Name = "boxGeo";

    ThrowIfFailed(D3DCreateBlob(vbByteSize, &geo->VertexBufferCPU));
    CopyMemory(geo->VertexBufferCPU->GetBufferPointer(), vertices.data(), vbByteSize);

    ThrowIfFailed(D3DCreateBlob(ibByteSize, &geo->IndexBufferCPU));
    CopyMemory(geo->IndexBufferCPU->GetBufferPointer(), indices.data(), ibByteSize);

    geo->VertexBufferGPU = d3dUtil::CreateDefaultBuffer(md3dDevice.Get(),
        mCommandList.Get(), vertices.data(), vbByteSize, geo->VertexBufferUploader);

    geo->IndexBufferGPU = d3dUtil::CreateDefaultBuffer(md3dDevice.Get(),
        mCommandList.Get(), indices.data(), ibByteSize, geo->IndexBufferUploader);

    geo->VertexBufferGPU->SetName(L"Vertex Buffer");
    geo->IndexBufferGPU->SetName(L"Index Buffer");

    geo->VertexByteStride = sizeof(Vertex);
    geo->VertexBufferByteSize = vbByteSize;
    geo->IndexFormat = DXGI_FORMAT_R16_UINT;
    geo->IndexBufferByteSize = ibByteSize;

    geo->DrawArgs["box"] = boxSubmesh;

    mGeometries[geo->Name] = std::move(geo);
}

void Renderer::BuildMaterials()
{
    auto grass = std::make_unique<Material>();
    grass->Name = "grass";
    grass->MatCBIndex = 0;
    grass->DiffuseSrvHeapIndex = 0;
    grass->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
    grass->FresnelR0 = XMFLOAT3(0.05f, 0.05f, 0.05f);
    grass->Roughness = 0.2f;

    mMaterials["grass"] = std::move(grass);
}

void Renderer::BuildRenderItems()
{
    int index = 0;

    // Map Data
    for (float z = -3; z < 3; z++) {
        for (float x = -3; x < 3; x++) {
            auto boxitem = std::make_unique<RenderItem>();
            //XMStoreFloat4x4(&boxitem->World, XMMatrixTranslation(x, GetTerrainHeight(x, z) , z));
            XMStoreFloat4x4(&boxitem->World, XMMatrixTranslation(x, 0.5 , z));
            XMStoreFloat4x4(&boxitem->TexTransform, XMMatrixScaling(1.0f, 1.0f, 1.0f));
            boxitem->ObjCBIndex = index++;
            boxitem->Mat = mMaterials["grass"].get();
            boxitem->Geo = mGeometries["boxGeo"].get();
            boxitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
            boxitem->IndexCount = boxitem->Geo->DrawArgs["box"].IndexCount;
            boxitem->StartIndexLocation = boxitem->Geo->DrawArgs["box"].StartIndexLocation;
            boxitem->BaseVertexLocation = boxitem->Geo->DrawArgs["box"].BaseVertexLocation;
            mAllRitems.push_back(std::move(boxitem));
        }
    }

    // Character Mesh
    for (UINT i = 0; i < meshes.size(); i++) {
        auto mesh = std::make_unique<RenderItem>();
        XMMATRIX world = XMLoadFloat4x4(&mesh->World);

        XMMATRIX Translation = XMMatrixTranslation(1.0f, 1.0f, 100.0f);
        XMMATRIX Scaling = XMMatrixScaling(0.01f, 0.01f, 0.01f);
        XMMATRIX yRotation = XMMatrixRotationX(XMConvertToRadians(-90.0f));

        XMMATRIX TRSMatrix = XMMatrixMultiply(XMMatrixMultiply(Translation, yRotation), Scaling);
        XMMatrixMultiply(world, TRSMatrix);

        XMStoreFloat4x4(&mesh->World, TRSMatrix);

        mesh->ObjCBIndex = index++;
        mesh->Geo = mGeometries["Character"].get();
        mesh->Mat = mMaterials["grass"].get();
        mesh->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
        mesh->IndexCount = mesh->Geo->DrawArgs[meshes[i].MeshName].IndexCount;
        mesh->StartIndexLocation = mesh->Geo->DrawArgs[meshes[i].MeshName].StartIndexLocation;
        mesh->BaseVertexLocation = mesh->Geo->DrawArgs[meshes[i].MeshName].BaseVertexLocation;
        mAllRitems.push_back(std::move(mesh));
    }

    // All the render items are opaque.
    for (auto& e : mAllRitems) {
        mOpaqueRitems.push_back(e.get());
    }
}

void Renderer::BuildFrameResources()
{
    for (int i = 0; i < gNumFrameResources; ++i)
    {
        mFrameResources.push_back(std::make_unique<FrameResource>(md3dDevice.Get(), 1, (UINT)mAllRitems.size(), (UINT)mMaterials.size()));
    }
}

void Renderer::BuildDescriptorHeaps()
{
    // Create SRV heap
    D3D12_DESCRIPTOR_HEAP_DESC srvHeapDesc = {};
    srvHeapDesc.NumDescriptors = 2;
    srvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    srvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    ThrowIfFailed(md3dDevice->CreateDescriptorHeap(&srvHeapDesc, IID_PPV_ARGS(&mSrvDescriptorHeap)));

    CD3DX12_CPU_DESCRIPTOR_HANDLE srvDescriptor(mSrvDescriptorHeap->GetCPUDescriptorHandleForHeapStart());

    // Table 0 : grass Texture
    auto grassTex = mTextures["grassTex"]->Resource;

    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Format = grassTex->GetDesc().Format;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MostDetailedMip = 0;
    srvDesc.Texture2D.MipLevels = grassTex->GetDesc().MipLevels;
    srvDesc.Texture2D.ResourceMinLODClamp = 0.0f;

    md3dDevice->CreateShaderResourceView(grassTex.Get(), &srvDesc, srvDescriptor);
}

void Renderer::UpdateObjectCBs(const GameTimer& gt)
{
    auto currObjectCB = mCurrFrameResource->ObjectCB.get();
    currObjectCB->Resource()->SetName(L"Object Constant Buffer");
    for (auto& e : mAllRitems)
    {
        // Only update the cbuffer data if the constants have changed.  
        // This needs to be tracked per frame resource.
        if (e->NumFramesDirty > 0)
        {
            XMMATRIX world = XMLoadFloat4x4(&e->World);
            XMMATRIX texTransform = XMLoadFloat4x4(&e->TexTransform);

            ObjectConstants objConstants;
            XMStoreFloat4x4(&objConstants.World, XMMatrixTranspose(world));
            XMStoreFloat4x4(&objConstants.TexTransform, XMMatrixTranspose(texTransform));

            currObjectCB->CopyData(e->ObjCBIndex, objConstants);

            // Next FrameResource need to be updated too.
            e->NumFramesDirty--;
        }
    }
}

void Renderer::UpdateMaterialCBs(const GameTimer& gt)
{
    auto currMaterialCB = mCurrFrameResource->MaterialCB.get();
    currMaterialCB->Resource()->SetName(L"Material Constant Buffer");
    for (auto& e : mMaterials)
    {
        // Only update the cbuffer data if the constants have changed.  If the cbuffer
        // data changes, it needs to be updated for each FrameResource.
        Material* mat = e.second.get();
        if (mat->NumFramesDirty > 0)
        {
            XMMATRIX matTransform = XMLoadFloat4x4(&mat->MatTransform);

            MaterialConstants matConstants;
            matConstants.DiffuseAlbedo = mat->DiffuseAlbedo;
            matConstants.FresnelR0 = mat->FresnelR0;
            matConstants.Roughness = mat->Roughness;
            XMStoreFloat4x4(&matConstants.MatTransform, XMMatrixTranspose(matTransform));

            currMaterialCB->CopyData(mat->MatCBIndex, matConstants);

            // Next FrameResource need to be updated too.
            mat->NumFramesDirty--;
        }
    }
}

void Renderer::UpdateMainPassCB(const GameTimer& gt)
{
    XMMATRIX view = mCamera.GetView();
    XMMATRIX proj = mCamera.GetProj();

    XMMATRIX viewProj = XMMatrixMultiply(view, proj);
    XMMATRIX invView = XMMatrixInverse(&XMMatrixDeterminant(view), view);
    XMMATRIX invProj = XMMatrixInverse(&XMMatrixDeterminant(proj), proj);
    XMMATRIX invViewProj = XMMatrixInverse(&XMMatrixDeterminant(viewProj), viewProj);

    XMStoreFloat4x4(&mMainPassCB.View, XMMatrixTranspose(view));
    XMStoreFloat4x4(&mMainPassCB.InvView, XMMatrixTranspose(invView));
    XMStoreFloat4x4(&mMainPassCB.Proj, XMMatrixTranspose(proj));
    XMStoreFloat4x4(&mMainPassCB.InvProj, XMMatrixTranspose(invProj));
    XMStoreFloat4x4(&mMainPassCB.ViewProj, XMMatrixTranspose(viewProj));
    XMStoreFloat4x4(&mMainPassCB.InvViewProj, XMMatrixTranspose(invViewProj));
    mMainPassCB.EyePosW = mCamera.GetPosition3f();
    mMainPassCB.RenderTargetSize = XMFLOAT2((float)mClientWidth, (float)mClientHeight);
    mMainPassCB.InvRenderTargetSize = XMFLOAT2(1.0f / mClientWidth, 1.0f / mClientHeight);
    mMainPassCB.NearZ = 1.0f;
    mMainPassCB.FarZ = 1000.0f;
    mMainPassCB.TotalTime = gt.TotalTime();
    mMainPassCB.DeltaTime = gt.DeltaTime();
    mMainPassCB.AmbientLight = { 0.25f, 0.25f, 0.35f, 1.0f };
    mMainPassCB.Lights[0].Direction = { 0.57735f, -0.57735f, 0.57735f };
    mMainPassCB.Lights[0].Strength = { 0.6f, 0.6f, 0.6f };
    mMainPassCB.Lights[1].Direction = { -0.57735f, -0.57735f, 0.57735f };
    mMainPassCB.Lights[1].Strength = { 0.3f, 0.3f, 0.3f };
    mMainPassCB.Lights[2].Direction = { 0.0f, -0.707f, -0.707f };
    mMainPassCB.Lights[2].Strength = { 0.15f, 0.15f, 0.15f };

    auto currPassCB = mCurrFrameResource->PassCB.get();
    currPassCB->CopyData(0, mMainPassCB);
}

void Renderer::LoadTextures()
{
    std::unique_ptr<uint8_t[]> textureData;
    std::vector<D3D12_SUBRESOURCE_DATA> subresources;

    auto grassTex = std::make_unique<Texture>();
    grassTex->Name = "grassTex";

    ThrowIfFailed(LoadDDSTextureFromFile(md3dDevice.Get(), L"Textures/grass.dds", &grassTex->Resource, textureData, subresources));
    grassTex->Resource->SetName(L"grass Texture");
    
    const UINT64 uploadBufferSize = GetRequiredIntermediateSize(grassTex->Resource.Get(), 0, static_cast<UINT>(subresources.size()));

    CD3DX12_HEAP_PROPERTIES heapProps(D3D12_HEAP_TYPE_UPLOAD);

    ThrowIfFailed(
        md3dDevice->CreateCommittedResource(
            &heapProps,
            D3D12_HEAP_FLAG_NONE,
            &CD3DX12_RESOURCE_DESC::Buffer(uploadBufferSize),
            D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr,
            IID_PPV_ARGS(&grassTex->UploadHeap)));

    UpdateSubresources(mCommandList.Get(), grassTex->Resource.Get(), grassTex->UploadHeap.Get(),
        0, 0, static_cast<UINT>(subresources.size()), subresources.data());
    mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(grassTex->Resource.Get(),
        D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE));


    mTextures[grassTex->Name] = std::move(grassTex);
}

void Renderer::LoadCharacters()
{
    FbxManager* mfbxManager = FbxManager::Create();
    FbxIOSettings* ios = FbxIOSettings::Create(mfbxManager, IOSROOT);
    mfbxManager->SetIOSettings(ios);
    FbxImporter* mfbxImporter = FbxImporter::Create(mfbxManager, "");
    FbxScene* mfbxScene = FbxScene::Create(mfbxManager, "");

    mfbxImporter->Initialize("Models/Remy.fbx", -1, mfbxManager->GetIOSettings());
    mfbxImporter->Import(mfbxScene);

    mfbxScene->GetGlobalSettings().SetAxisSystem(FbxAxisSystem::DirectX);

    FbxGeometryConverter geometryConverter(mfbxManager);
    geometryConverter.Triangulate(mfbxScene, true);

    mfbxImporter->Destroy();

    FbxNode* lRootNode = mfbxScene->GetRootNode();

    std::vector<Vertex> vertices;
    std::vector<std::uint16_t> indices;

    for (int k = 0; k < lRootNode->GetChildCount(); k++) {
        FbxMeshData meshdata;

        FbxNode* mNode = lRootNode->GetChild(k);

        FbxNodeAttribute* attribute = mNode->GetNodeAttribute();

        if (attribute->GetAttributeType() == FbxNodeAttribute::eMesh) {
            FbxMesh* mesh = mNode->GetMesh();
            int vertexcount = mesh->GetControlPointsCount();

            FbxVector4* controlPoints = mesh->GetControlPoints();
            for (int i = 0; i < vertexcount; ++i) {
                Vertex tempvertex;
                tempvertex.Pos.x = static_cast<float>(controlPoints[i].mData[0]);
                tempvertex.Pos.y = static_cast<float>(controlPoints[i].mData[2]);
                tempvertex.Pos.z = static_cast<float>(controlPoints[i].mData[1]);

                vertices.push_back(tempvertex);
            }

            uint16_t arrIdx[3];
            int polygonCount = mesh->GetPolygonCount();


            for (int i = 0; i < polygonCount; i++) // 삼각형의 개수
            {
                for (int j = 0; j < 3; j++) // 삼각형은 세 개의 정점으로 구성
                {
                    uint16_t controlPointIndex = mesh->GetPolygonVertex(i, j); // 제어점의 인덱스 추출
                    arrIdx[j] = controlPointIndex;
                }

                indices.push_back(arrIdx[0]);
                indices.push_back(arrIdx[2]);
                indices.push_back(arrIdx[1]);

            }

            meshdata.MeshName = mesh->GetName();
            meshdata.VertexSize = vertexcount;
            meshdata.IndexSize = polygonCount * 3;

            meshes.push_back(meshdata);
        }
    }

    const UINT vbByteSize = (UINT)vertices.size() * sizeof(Vertex);
    const UINT ibByteSize = (UINT)indices.size() * sizeof(std::uint16_t);

    auto geo = std::make_unique<MeshGeometry>();
    geo->Name = "Character";

    ThrowIfFailed(D3DCreateBlob(vbByteSize, &geo->VertexBufferCPU));
    CopyMemory(geo->VertexBufferCPU->GetBufferPointer(), vertices.data(), vbByteSize);

    ThrowIfFailed(D3DCreateBlob(ibByteSize, &geo->IndexBufferCPU));
    CopyMemory(geo->IndexBufferCPU->GetBufferPointer(), indices.data(), ibByteSize);

    geo->VertexBufferGPU = d3dUtil::CreateDefaultBuffer(md3dDevice.Get(),
        mCommandList.Get(), vertices.data(), vbByteSize, geo->VertexBufferUploader);

    geo->IndexBufferGPU = d3dUtil::CreateDefaultBuffer(md3dDevice.Get(),
        mCommandList.Get(), indices.data(), ibByteSize, geo->IndexBufferUploader);

    geo->VertexByteStride = sizeof(Vertex);
    geo->VertexBufferByteSize = vbByteSize;
    geo->IndexFormat = DXGI_FORMAT_R16_UINT;
    geo->IndexBufferByteSize = ibByteSize;

    UINT indexlocation = 0;
    UINT vertexlocation = 0;

    for (int i = 0; i < meshes.size(); i++) {
        SubmeshGeometry submesh;
        submesh.IndexCount = meshes[i].IndexSize;
        submesh.StartIndexLocation = indexlocation;
        submesh.BaseVertexLocation = vertexlocation;

        indexlocation += meshes[i].IndexSize;
        vertexlocation += meshes[i].VertexSize;

        geo->DrawArgs[meshes[i].MeshName] = submesh;
    }

    mGeometries[geo->Name] = std::move(geo);
}

float Renderer::GetTerrainHeight(float x, float z)
{
    return round(0.2f * (z * sinf(0.1f * x) + x * cosf(0.1f * z)));
}

std::array<const CD3DX12_STATIC_SAMPLER_DESC, 6> Renderer::GetStaticSamplers()
{
    // Applications usually only need a handful of samplers.  So just define them all up front
    // and keep them available as part of the root signature.  

    const CD3DX12_STATIC_SAMPLER_DESC pointWrap(
        0, // shaderRegister
        D3D12_FILTER_MIN_MAG_MIP_POINT, // filter
        D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressU
        D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressV
        D3D12_TEXTURE_ADDRESS_MODE_WRAP); // addressW

    const CD3DX12_STATIC_SAMPLER_DESC pointClamp(
        1, // shaderRegister
        D3D12_FILTER_MIN_MAG_MIP_POINT, // filter
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressU
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressV
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP); // addressW

    const CD3DX12_STATIC_SAMPLER_DESC linearWrap(
        2, // shaderRegister
        D3D12_FILTER_MIN_MAG_MIP_LINEAR, // filter
        D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressU
        D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressV
        D3D12_TEXTURE_ADDRESS_MODE_WRAP); // addressW

    const CD3DX12_STATIC_SAMPLER_DESC linearClamp(
        3, // shaderRegister
        D3D12_FILTER_MIN_MAG_MIP_LINEAR, // filter
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressU
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressV
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP); // addressW

    const CD3DX12_STATIC_SAMPLER_DESC anisotropicWrap(
        4, // shaderRegister
        D3D12_FILTER_ANISOTROPIC, // filter
        D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressU
        D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressV
        D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressW
        0.0f,                             // mipLODBias
        8);                               // maxAnisotropy

    const CD3DX12_STATIC_SAMPLER_DESC anisotropicClamp(
        5, // shaderRegister
        D3D12_FILTER_ANISOTROPIC, // filter
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressU
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressV
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressW
        0.0f,                              // mipLODBias
        8);                                // maxAnisotropy

    return {
        pointWrap, pointClamp,
        linearWrap, linearClamp,
        anisotropicWrap, anisotropicClamp };
}
