#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

// This exports D3D12SDKVersion and D3D12SDKPath.
#include <d3d12agility.hpp>

#include <directx/d3d12.h>
#include <directx/dxcore.h>
#include <directx/d3dx12.h>
#include <dxguids/dxguids.h>
#include <dxgi1_6.h>
#include <dxcapi.h>
#include <comdef.h>
#include <d3dcompiler.h>

#include <pix3.h>

#include <wrl.h>
using namespace Microsoft::WRL;

#include <iostream>
#include <sstream>
#include <fstream>
#include <string>
#include <array>
#include <numeric>

constexpr auto INPUT_WIDTH = 1152u;
constexpr auto INPUT_HEIGHT = 1152u;
constexpr auto IMAGE_COUNT = 2u;
constexpr auto OUTPUT_WIDTH = 800u;
constexpr auto OUTPUT_HEIGHT = 600u;

#define USE_WARP 0;

void waitFor(ID3D12Fence* fence, UINT64 fenceValue)
{
	if (fence->GetCompletedValue() < fenceValue)
	{
		HANDLE eventHandle = ::CreateEvent(nullptr, false, false, nullptr);
		HRESULT hr = fence->SetEventOnCompletion(fenceValue, eventHandle);

		if (SUCCEEDED(hr))
			::WaitForSingleObject(eventHandle, INFINITE);

		::CloseHandle(eventHandle);
		
		if (FAILED(hr))
			throw;
	}
}

void mapFrameToBuffer(const std::array<D3D12_PLACED_SUBRESOURCE_FOOTPRINT, IMAGE_COUNT * 2>& footprints, int frameIndex, int planeIndex, char* buffer, const char* frameData)
{
	const auto subresource = ::D3D12CalcSubresource(0, frameIndex, planeIndex, 1, IMAGE_COUNT);
	const auto planeHeight = footprints[subresource].Footprint.Height;
	const auto rowPitch = footprints[subresource].Footprint.RowPitch;
	int outputOffset = footprints[subresource].Offset;

	for (int row{ 0 }, inputOffset{ 0 }; row < planeHeight; ++row, inputOffset += INPUT_WIDTH, outputOffset += rowPitch)
		::memcpy_s(reinterpret_cast<void*>(buffer + outputOffset), rowPitch, reinterpret_cast<const void*>(frameData + inputOffset), INPUT_WIDTH);
}

LRESULT CALLBACK WindowCallback(HWND Window, UINT Message, WPARAM WParam, LPARAM LParam)
{
	return DefWindowProc(Window, Message, WParam, LParam);
}

int main(int argc, char* argv[])
{
	// Attempt to load PIX.
	auto pixModule = ::PIXLoadLatestWinPixGpuCapturerLibrary();
	
	if (pixModule == nullptr)
	{
		std::cout << "Unable to load PIX." << std::endl;
		return -1;
	}

	// Start capture.
	PIXCaptureParameters captureParams = { };
	captureParams.GpuCaptureParameters.FileName = L"capture.wpix";
	::PIXBeginCapture(PIX_CAPTURE_GPU, &captureParams);

	// Initialize D3D.
    ComPtr<IDXGIFactory7> factory;
    ::CreateDXGIFactory2(DXGI_CREATE_FACTORY_DEBUG, IID_PPV_ARGS(&factory));

    ComPtr<IDXGIAdapter> adapter;

#ifdef USE_WARP
	factory->EnumWarpAdapter(IID_PPV_ARGS(&adapter));
#else
    factory->EnumAdapterByGpuPreference(0, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, IID_PPV_ARGS(&adapter));
#endif
    DXGI_ADAPTER_DESC adapterDesc;
    adapter->GetDesc(&adapterDesc);

    std::wcout << "Running on adapter " << adapterDesc.Description << "..." << std::endl;

    ComPtr<ID3D12Debug> debugInterface;
    ::D3D12GetDebugInterface(IID_PPV_ARGS(&debugInterface));
    debugInterface->EnableDebugLayer();

    ComPtr<ID3D12Device10> device;
    ::D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&device));

    ComPtr<ID3D12InfoQueue> infoQueue;
    device->QueryInterface(IID_PPV_ARGS(&infoQueue));
    infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION, TRUE);
    infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, TRUE);
    infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_WARNING, TRUE);
    //InfoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_INFO, TRUE);
    D3D12_MESSAGE_ID suppressIds[] = { D3D12_MESSAGE_ID_CLEARRENDERTARGETVIEW_MISMATCHINGCLEARVALUE, D3D12_MESSAGE_ID_CLEARDEPTHSTENCILVIEW_MISMATCHINGCLEARVALUE };
    D3D12_MESSAGE_SEVERITY severities[] = { D3D12_MESSAGE_SEVERITY_INFO };
    D3D12_INFO_QUEUE_FILTER infoQueueFilter = {};
    infoQueueFilter.DenyList.NumIDs = _countof(suppressIds);
    infoQueueFilter.DenyList.pIDList = suppressIds;
    infoQueueFilter.DenyList.NumSeverities = _countof(severities);
    infoQueueFilter.DenyList.pSeverityList = severities;
    infoQueue->PushStorageFilter(&infoQueueFilter);

    // Create command queue.
    ComPtr<ID3D12CommandQueue> commandQueue;
    D3D12_COMMAND_QUEUE_DESC commandQueueDesc = { D3D12_COMMAND_LIST_TYPE_COMPUTE, D3D12_COMMAND_QUEUE_PRIORITY_NORMAL, D3D12_COMMAND_QUEUE_FLAG_NONE, 0x00 };
    device->CreateCommandQueue(&commandQueueDesc, IID_PPV_ARGS(&commandQueue));

    // Create command list and a fence.
    ComPtr<ID3D12CommandAllocator> commandAllocator;
    ComPtr<ID3D12GraphicsCommandList> commandList;
    device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_COMPUTE, IID_PPV_ARGS(&commandAllocator));
    device->CreateCommandList(0x00, D3D12_COMMAND_LIST_TYPE_COMPUTE, commandAllocator.Get(), nullptr, IID_PPV_ARGS(&commandList));

    // Create a fence.
    ComPtr<ID3D12Fence> fence;
    device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence));

	// Compile the shader.
	ComPtr<IDxcBlob> shaderBytecode;
	std::cout << "Compiling shader sources..." << std::endl;

	{
		ComPtr<IDxcLibrary> library;
		ComPtr<IDxcCompiler> compiler;
		ComPtr<IDxcBlobEncoding> sourceBlob;
		ComPtr<IDxcOperationResult> result;
		UINT32 codePage = CP_UTF8;

		std::vector<LPCWSTR> compileOptions;

		compileOptions.push_back(L"-Zi");
		compileOptions.push_back(L"-Qembed_debug");

		::DxcCreateInstance(CLSID_DxcLibrary, IID_PPV_ARGS(&library));
		::DxcCreateInstance(CLSID_DxcCompiler, IID_PPV_ARGS(&compiler));
		library->CreateBlobFromFile(L"sample.hlsl", &codePage, &sourceBlob);
		compiler->Compile(sourceBlob.Get(), L"sample.hlsl", L"kernel_sampleYUV", L"cs_6_0", 
			compileOptions.data(), compileOptions.size(), nullptr, 0, nullptr, &result);

		// Check if compilation was successful.
		HRESULT hr;
		result->GetStatus(&hr);

		if (!SUCCEEDED(hr))
		{
			ComPtr<IDxcBlobEncoding> errors;
			result->GetErrorBuffer(&errors);
			std::string error(reinterpret_cast<char*>(errors->GetBufferPointer()), errors->GetBufferSize());
			std::cout << "Error compiling shader: " << error << std::endl;

			return -2;
		}

		result->GetResult(&shaderBytecode);
	}

    // Setup root signature.
	std::array<D3D12_DESCRIPTOR_RANGE1, 3> descriptorRanges;
	descriptorRanges[0] = {};
	descriptorRanges[0].BaseShaderRegister = 0;
	descriptorRanges[0].RegisterSpace = 0;
	descriptorRanges[0].Flags = D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE | D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC_WHILE_SET_AT_EXECUTE;
	descriptorRanges[0].NumDescriptors = 1;
	descriptorRanges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
	descriptorRanges[0].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
	descriptorRanges[1] = {};
	descriptorRanges[1].BaseShaderRegister = 1;
	descriptorRanges[1].RegisterSpace = 0;
	descriptorRanges[1].Flags = D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE | D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC_WHILE_SET_AT_EXECUTE;
	descriptorRanges[1].NumDescriptors = 1;
	descriptorRanges[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
	descriptorRanges[1].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
	descriptorRanges[2] = {};
	descriptorRanges[2].BaseShaderRegister = 2;
	descriptorRanges[2].RegisterSpace = 0;
	descriptorRanges[2].Flags = D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE;
	descriptorRanges[2].NumDescriptors = 1;
	descriptorRanges[2].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
	descriptorRanges[2].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

	D3D12_ROOT_DESCRIPTOR_TABLE1 descriptorTable{};
	descriptorTable.NumDescriptorRanges = descriptorRanges.size();
	descriptorTable.pDescriptorRanges = descriptorRanges.data();

	D3D12_ROOT_PARAMETER1 rootParameter{};
	rootParameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
	rootParameter.DescriptorTable.NumDescriptorRanges = descriptorRanges.size();
	rootParameter.DescriptorTable.pDescriptorRanges = descriptorRanges.data();
	rootParameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

	D3D12_STATIC_SAMPLER_DESC samplerInfo{};
	samplerInfo.AddressU = D3D12_TEXTURE_ADDRESS_MODE_MIRROR;
	samplerInfo.AddressV = D3D12_TEXTURE_ADDRESS_MODE_MIRROR;
	samplerInfo.AddressW = D3D12_TEXTURE_ADDRESS_MODE_MIRROR;
	samplerInfo.Filter = D3D12_FILTER_COMPARISON_MIN_MAG_MIP_LINEAR;
	samplerInfo.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
	samplerInfo.MinLOD = 0.f;
	samplerInfo.MaxLOD = 1.f;
	samplerInfo.ShaderRegister = 0;
	samplerInfo.RegisterSpace = 1;
	samplerInfo.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

	D3D12_VERSIONED_ROOT_SIGNATURE_DESC rootSignatureDesc{};
	rootSignatureDesc.Version = D3D_ROOT_SIGNATURE_VERSION_1_1;
	rootSignatureDesc.Desc_1_1.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;
	rootSignatureDesc.Desc_1_1.NumParameters = 1;
	rootSignatureDesc.Desc_1_1.pParameters = &rootParameter;
	rootSignatureDesc.Desc_1_1.NumStaticSamplers = 1;
	rootSignatureDesc.Desc_1_1.pStaticSamplers = &samplerInfo;

	// Serialize and create root signature.
	ComPtr<ID3DBlob> signature, errors;
	ComPtr<ID3D12RootSignature> rootSignature;
	::D3D12SerializeVersionedRootSignature(&rootSignatureDesc, &signature, &errors);
	device->CreateRootSignature(0x00, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS(&rootSignature));

	// Create compute pipeline state.
	ComPtr<ID3D12PipelineState> pipeline;
	D3D12_COMPUTE_PIPELINE_STATE_DESC pipelineDesc{};
	pipelineDesc.pRootSignature = rootSignature.Get();
	pipelineDesc.CS.pShaderBytecode = shaderBytecode->GetBufferPointer();
	pipelineDesc.CS.BytecodeLength = shaderBytecode->GetBufferSize();
	device->CreateComputePipelineState(&pipelineDesc, IID_PPV_ARGS(&pipeline));

	// Create descriptor heap.
	ComPtr<ID3D12DescriptorHeap> descriptorHeap;
	D3D12_DESCRIPTOR_HEAP_DESC descriptorHeapDesc = { D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 3, D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE };
	device->CreateDescriptorHeap(&descriptorHeapDesc, IID_PPV_ARGS(&descriptorHeap));

	// Setup the resources.
	D3D12_HEAP_PROPERTIES uploadHeap = { D3D12_HEAP_TYPE_UPLOAD, D3D12_CPU_PAGE_PROPERTY_UNKNOWN, D3D12_MEMORY_POOL_UNKNOWN, 0, 0 };
	D3D12_HEAP_PROPERTIES remoteHeap = { D3D12_HEAP_TYPE_DEFAULT, D3D12_CPU_PAGE_PROPERTY_UNKNOWN, D3D12_MEMORY_POOL_UNKNOWN, 0, 0 };

	// Create input frame resources. Start by describing the input frame texture and acquiring the footprint for it in order to determine the required upload buffer size.
	ComPtr<ID3D12Resource> inputFrames;
	D3D12_RESOURCE_DESC inputFrameTextureDesc = { D3D12_RESOURCE_DIMENSION_TEXTURE2D, 0, (UINT64)INPUT_WIDTH, (UINT)INPUT_HEIGHT, (UINT16)IMAGE_COUNT, 1, DXGI_FORMAT_NV12, { 1, 0 }, D3D12_TEXTURE_LAYOUT_UNKNOWN, D3D12_RESOURCE_FLAG_NONE };
	device->CreateCommittedResource(&remoteHeap, D3D12_HEAP_FLAG_NONE, &inputFrameTextureDesc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&inputFrames));

	std::array<D3D12_PLACED_SUBRESOURCE_FOOTPRINT, IMAGE_COUNT * 2> inputFrameUploadFootprints;
	device->GetCopyableFootprints(&inputFrameTextureDesc, 0, inputFrameUploadFootprints.size(), 0, inputFrameUploadFootprints.data(), nullptr, nullptr, nullptr);

	// Compute buffer sizes. Since the input frames are NV12 encoded (YUV 4:2:0), each pixel takes up 12 bits (= 1.5 bytes). Output buffer is b * c * h * w * sizeof(float).
	UINT64 bufferSize = std::accumulate(inputFrameUploadFootprints.begin(), inputFrameUploadFootprints.end(), 0ull, [](UINT64 s, const auto& f) { return s + f.Footprint.Height * f.Footprint.RowPitch; });
	UINT64 outputStride = OUTPUT_WIDTH * OUTPUT_HEIGHT;	// 1 channel in output buffer.
	UINT64 outputSize = IMAGE_COUNT * 3 * outputStride * sizeof(float);

	ComPtr<ID3D12Resource> inputFramesUploadBuffer;
	D3D12_RESOURCE_DESC uploadBufferDesc = { D3D12_RESOURCE_DIMENSION_BUFFER, 0, bufferSize, 1, 1, 1, DXGI_FORMAT_UNKNOWN, { 1, 0 }, D3D12_TEXTURE_LAYOUT_ROW_MAJOR, D3D12_RESOURCE_FLAG_NONE };
	device->CreateCommittedResource(&uploadHeap, D3D12_HEAP_FLAG_NONE, &uploadBufferDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&inputFramesUploadBuffer));

	// Define input frame views.
	D3D12_SHADER_RESOURCE_VIEW_DESC lumaViewDesc = { DXGI_FORMAT_R8_UNORM,   D3D12_SRV_DIMENSION_TEXTURE2DARRAY, D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING };
	D3D12_SHADER_RESOURCE_VIEW_DESC chromaViewDesc = { DXGI_FORMAT_R8G8_UNORM, D3D12_SRV_DIMENSION_TEXTURE2DARRAY, D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING };
	lumaViewDesc.Texture2DArray = { 0, 1, 0, (UINT)IMAGE_COUNT, 0, 0.0f };  // Plane 0
	chromaViewDesc.Texture2DArray = { 0, 1, 0, (UINT)IMAGE_COUNT, 1, 0.0f };  // Plane 1

	// Create the result array resource.
	ComPtr<ID3D12Resource> outputUAV;
	D3D12_RESOURCE_DESC resultBufferDesc = { D3D12_RESOURCE_DIMENSION_BUFFER, 0, (UINT64)outputSize, 1, 1, 1, DXGI_FORMAT_UNKNOWN, { 1, 0 }, D3D12_TEXTURE_LAYOUT_ROW_MAJOR, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS };
	D3D12_UNORDERED_ACCESS_VIEW_DESC resultViewDesc = { DXGI_FORMAT_UNKNOWN, D3D12_UAV_DIMENSION_BUFFER };
	device->CreateCommittedResource(&remoteHeap, D3D12_HEAP_FLAG_NONE, &resultBufferDesc, D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&outputUAV));

	// Get descriptor size and first descriptor handle.
	UINT64 descriptorSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	auto descriptorHandle = descriptorHeap->GetCPUDescriptorHandleForHeapStart();

	device->CreateShaderResourceView(inputFrames.Get(), &lumaViewDesc, descriptorHandle);
	descriptorHandle.ptr += descriptorSize;
	device->CreateShaderResourceView(inputFrames.Get(), &chromaViewDesc, descriptorHandle);
	descriptorHandle.ptr += descriptorSize;
	resultViewDesc.Buffer = { 0, (UINT)(outputSize / sizeof(float)), sizeof(float), 0, D3D12_BUFFER_UAV_FLAG_NONE };
	device->CreateUnorderedAccessView(outputUAV.Get(), nullptr, &resultViewDesc, descriptorHandle);
	descriptorHandle.ptr += descriptorSize;

	// Transition all resources into their expected initial state.
	const D3D12_RESOURCE_BARRIER initBarriers[] = {
		{ D3D12_RESOURCE_BARRIER_TYPE_TRANSITION, D3D12_RESOURCE_BARRIER_FLAG_NONE, { outputUAV.Get(), D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_UNORDERED_ACCESS } },
		{ D3D12_RESOURCE_BARRIER_TYPE_TRANSITION, D3D12_RESOURCE_BARRIER_FLAG_NONE, { inputFrames.Get(), D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE } }
	};

	commandList->ResourceBarrier(2, initBarriers);

	// Submit command list and wait for initialization commands to finish.
	commandList->Close();

	// Submit the command list.
	{
		ID3D12CommandList* commandLists[] = { commandList.Get() };
		commandQueue->ExecuteCommandLists(1, commandLists);
	}

	// Wait for the command queue to finish.
	commandQueue->Signal(fence.Get(), 1);
	::waitFor(fence.Get(), 1);

	// Load the resources.
	std::cout << "Loading image resources..." << std::endl;
	std::array<char*, IMAGE_COUNT> lumaPlanes, chromaPlanes;

	for (int i = 0; i < IMAGE_COUNT; i++)
	{
		std::cout << "Image " << i << "...";

		std::stringstream lumaFileName;
		lumaFileName << "luma_" << i << ".nv12";
		auto lumaStream = std::ifstream(lumaFileName.str(), std::ios::binary);

		if (!lumaStream.is_open())
		{
			std::cout << " error." << std::endl;
			throw;
		}

		lumaPlanes[i] = new char[INPUT_WIDTH * INPUT_HEIGHT];
		lumaStream.read(lumaPlanes[i], INPUT_WIDTH * INPUT_HEIGHT);

		std::stringstream chromaFileName;
		chromaFileName << "chroma_" << i << ".nv12";
		auto chromaStream = std::ifstream(chromaFileName.str(), std::ios::binary);

		if (!chromaStream.is_open())
		{
			std::cout << " error." << std::endl;
			throw;
		}

		chromaPlanes[i] = new char[INPUT_WIDTH * INPUT_HEIGHT];
		chromaStream.read(chromaPlanes[i], INPUT_WIDTH * INPUT_HEIGHT);

		std::cout << " done." << std::endl;
	}

	// Reset command list and allocator.
	commandAllocator->Reset();
	commandList->Reset(commandAllocator.Get(), nullptr);

    // Upload resources.
	std::cout << "Everything is setup. Start uploading resources..." << std::endl;

	static const D3D12_RESOURCE_BARRIER copyBarriers[] = {
		{ D3D12_RESOURCE_BARRIER_TYPE_TRANSITION, D3D12_RESOURCE_BARRIER_FLAG_NONE, { inputFrames.Get(), D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,  D3D12_RESOURCE_STATE_COPY_DEST} },
		{ D3D12_RESOURCE_BARRIER_TYPE_TRANSITION, D3D12_RESOURCE_BARRIER_FLAG_NONE, { inputFrames.Get(), D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE } }
	};

	commandList->ResourceBarrier(1, &copyBarriers[0]);

	// First, map the chroma and luma planes into the upload textures.
	D3D12_RANGE mappedRange = {};
	char* buffer;
	inputFramesUploadBuffer->Map(0, &mappedRange, reinterpret_cast<void**>(&buffer));

	// Copy each frame individually.
	for (int i{ 0 }; i < IMAGE_COUNT; ++i)
	{
		::mapFrameToBuffer(inputFrameUploadFootprints, i, 0, buffer, lumaPlanes[i]);    // Luma plane.
		::mapFrameToBuffer(inputFrameUploadFootprints, i, 1, buffer, chromaPlanes[i]);  // Chroma plane.
	}

	// Unmap and verify.
	inputFramesUploadBuffer->Unmap(0, nullptr);

	// Perform the actual upload for each subresource.
	for (UINT32 sr{ 0 }; sr < inputFrameUploadFootprints.size(); ++sr)
	{
		D3D12_TEXTURE_COPY_LOCATION targetLocation = { inputFrames.Get(), D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX, sr };
		D3D12_TEXTURE_COPY_LOCATION sourceLocation = { inputFramesUploadBuffer.Get(), D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT, inputFrameUploadFootprints[sr] };
		commandList->CopyTextureRegion(&targetLocation, 0, 0, 0, &sourceLocation, nullptr);
	}

	// Transition the input texture back so that it can be read from shaders.
	commandList->ResourceBarrier(1, &copyBarriers[1]);

	// Dispatch compute shader.
	std::cout << "Dispatching shader..." << std::endl;

	ID3D12DescriptorHeap* descriptorHeaps[]{ descriptorHeap.Get() };
	commandList->SetDescriptorHeaps(1, descriptorHeaps);
	commandList->SetPipelineState(pipeline.Get());
	commandList->SetComputeRootSignature(rootSignature.Get());
	commandList->SetComputeRootDescriptorTable(0, descriptorHeap->GetGPUDescriptorHandleForHeapStart());
	commandList->Dispatch(OUTPUT_WIDTH / 8, OUTPUT_HEIGHT / 8, IMAGE_COUNT);

	// Submit command list and wait for initialization commands to finish.
	commandList->Close();

	// Submit the command list.
	{
		ID3D12CommandList* commandLists[] = { commandList.Get() };
		commandQueue->ExecuteCommandLists(1, commandLists);
	}

	// Wait for the command queue to finish.
	commandQueue->Signal(fence.Get(), 2);
	::waitFor(fence.Get(), 2);

	// Cleanup
	std::cout << "Cleaning up...";

	for (int i = 0; i < IMAGE_COUNT; ++i)
	{
		delete[] chromaPlanes[i];
		delete[] lumaPlanes[i];
	}

	::PIXEndCapture(false);
	::FreeLibrary(pixModule);

	std::cout << " finished." << std::endl;
    return 0;
}