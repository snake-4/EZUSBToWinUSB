#define NOMINMAX
#include <Windows.h>
#include <vector>
#include <cstdint>
#include <unordered_map>
#include <functional>
#include <optional>
#include <numeric>
#include <libusb-1.0/libusb.h>
#include "EZUSB/ezusb.h"
#include "Globals.h"
#include "TranslationLayer.h"

using namespace EZUSB;

struct ioctl_handler_return_t {
	ioctl_handler_return_t(DWORD _error)
		: error(_error) { }
	ioctl_handler_return_t(DWORD _error, DWORD _bytesReturned)
		: error(_error), bytesReturned(_bytesReturned) { }
	const DWORD error;
	const std::optional<DWORD> bytesReturned;
};
using ioctl_handler_t = std::function<ioctl_handler_return_t(LPVOID, DWORD, LPVOID, DWORD)>;

std::unordered_map<DWORD, ioctl_handler_t> ioctl_handler_map {

	{ IOCTL_Ezusb_VENDOR_REQUEST, [](LPVOID inBuffer, DWORD inBufferLen, LPVOID outBuffer, DWORD outBufferLen) -> ioctl_handler_return_t {
		/*
		* IOCTL_Ezusb_VENDOR_REQUEST
		*/
		if (inBufferLen < sizeof(VENDOR_REQUEST_IN))
			return ERROR_INVALID_PARAMETER;

		auto req = reinterpret_cast<PVENDOR_REQUEST_IN>(inBuffer);
		int status = 0;
		if (req->direction) {
			status = GUSBDev.value().ReadControlTransfer(LIBUSB_REQUEST_TYPE_VENDOR | LIBUSB_RECIPIENT_DEVICE,
				req->bRequest, req->wValue, req->wIndex, inBuffer, req->wLength); //nInBufferSize);
		}
		else {
			std::vector<uint8_t> writeBuffer(req->wLength, 0);
			if (req->wLength == 1)
				writeBuffer[0] = req->bData;
			else
				std::iota(writeBuffer.begin(), writeBuffer.end(), 0);

			status = GUSBDev.value().WriteControlTransfer(LIBUSB_REQUEST_TYPE_VENDOR | LIBUSB_RECIPIENT_DEVICE,
				req->bRequest, req->wValue, req->wIndex, writeBuffer.data(), req->wLength);
			status = std::max(status, 0); //lpBytesReturned is 0 on these requests
		}

		if (status > 0)
			return { ERROR_SUCCESS, static_cast<DWORD>(status) };
		else
			return ERROR_SUCCESS;
	} },

	{ IOCTL_EZUSB_BULK_READ, [](LPVOID inBuffer, DWORD inBufferLen, LPVOID outBuffer, DWORD outBufferLen) -> ioctl_handler_return_t {
		/*
		* IOCTL_EZUSB_BULK_READ
		*/
		if (inBufferLen < sizeof(BULK_TRANSFER_CONTROL))
			return ERROR_INVALID_PARAMETER;

		auto req = reinterpret_cast<PBULK_TRANSFER_CONTROL>(inBuffer);
		int bytesRead; //bytesRead is ignored

		int err = GUSBDev.value().ReadBulkTransfer(req->pipeNum, outBuffer, outBufferLen, &bytesRead);
		return (err == LIBUSB_SUCCESS ? ERROR_SUCCESS : ERROR_GEN_FAILURE);
	} },

	{ IOCTL_EZUSB_BULK_WRITE, [](LPVOID inBuffer, DWORD inBufferLen, LPVOID outBuffer, DWORD outBufferLen) -> ioctl_handler_return_t {
		/*
		* IOCTL_EZUSB_BULK_WRITE
		*/
		if (inBufferLen < sizeof(BULK_TRANSFER_CONTROL))
			return ERROR_INVALID_PARAMETER;

		auto req = reinterpret_cast<PBULK_TRANSFER_CONTROL>(inBuffer);
		int bytesWritten; //bytesWritten is ignored

		int err = GUSBDev.value().WriteBulkTransfer(req->pipeNum, outBuffer, outBufferLen, &bytesWritten);
		return (err == LIBUSB_SUCCESS ? ERROR_SUCCESS : ERROR_GEN_FAILURE);
	} },

	{ IOCTL_Ezusb_RESETPIPE, [](LPVOID inBuffer, DWORD inBufferLen, LPVOID outBuffer, DWORD outBufferLen) -> ioctl_handler_return_t {
		/*
		* IOCTL_EZUSB_BULK_WRITE
		*/
		return ERROR_SUCCESS;
	} },

	{ IOCTL_Ezusb_ABORTPIPE, [](LPVOID inBuffer, DWORD inBufferLen, LPVOID outBuffer, DWORD outBufferLen) -> ioctl_handler_return_t {
		/*
		* IOCTL_Ezusb_ABORTPIPE
		*/
		return ERROR_SUCCESS;
	} },

	{ IOCTL_EZUSB_ANCHOR_DOWNLOAD, [](LPVOID inBuffer, DWORD inBufferLen, LPVOID outBuffer, DWORD outBufferLen) -> ioctl_handler_return_t {
		/*
		* IOCTL_EZUSB_ANCHOR_DOWNLOAD
		*/
		if (inBufferLen < sizeof(ANCHOR_DOWNLOAD_CONTROL))
			return ERROR_INVALID_PARAMETER;

		static constexpr uint16_t ANCHOR_DOWNLOAD_CHUNK_SIZE = 64;
		auto req = reinterpret_cast<PANCHOR_DOWNLOAD_CONTROL>(inBuffer);

		int chunkCount = ((outBufferLen + ANCHOR_DOWNLOAD_CHUNK_SIZE - 1) / ANCHOR_DOWNLOAD_CHUNK_SIZE);
		for (int i = 0; i < chunkCount; i++)
		{
			uint16_t wLength = ((i == (chunkCount - 1)) &&
				(outBufferLen % ANCHOR_DOWNLOAD_CHUNK_SIZE)) ?
				(outBufferLen % ANCHOR_DOWNLOAD_CHUNK_SIZE) : ANCHOR_DOWNLOAD_CHUNK_SIZE;

			uint8_t bRequest = ANCHOR_LOAD_INTERNAL;
			uint16_t wValue = (i * ANCHOR_DOWNLOAD_CHUNK_SIZE) + req->Offset;

			const void* bufferAddr = reinterpret_cast<uint8_t*>(outBuffer) + i * ANCHOR_DOWNLOAD_CHUNK_SIZE;
			int transferred = GUSBDev.value().WriteControlTransfer(
				LIBUSB_REQUEST_TYPE_VENDOR | LIBUSB_RECIPIENT_DEVICE, bRequest, wValue, 0, bufferAddr, wLength
			);

			if (transferred != wLength) {
				return ERROR_GEN_FAILURE;
			}
		}

		return ERROR_SUCCESS; //lpBytesReturned is undefined
	} },

	{ IOCTL_EZUSB_VENDOR_OR_CLASS_REQUEST, [](LPVOID inBuffer, DWORD inBufferLen, LPVOID outBuffer, DWORD outBufferLen) -> ioctl_handler_return_t {
		/*
		* IOCTL_EZUSB_VENDOR_OR_CLASS_REQUEST
		*/
		if (inBufferLen < sizeof(VENDOR_OR_CLASS_REQUEST_CONTROL))
			return ERROR_INVALID_PARAMETER;

		auto req = (PVENDOR_OR_CLASS_REQUEST_CONTROL)inBuffer;
		uint8_t requestType = 0;
		switch ((req->requestType << 2) | req->recepient) {
		case 0x04: requestType = LIBUSB_REQUEST_TYPE_CLASS | LIBUSB_RECIPIENT_DEVICE; break;
		case 0x05: requestType = LIBUSB_REQUEST_TYPE_CLASS | LIBUSB_RECIPIENT_INTERFACE; break;
		case 0x06: requestType = LIBUSB_REQUEST_TYPE_CLASS | LIBUSB_RECIPIENT_ENDPOINT; break;
		case 0x07: requestType = LIBUSB_REQUEST_TYPE_CLASS | LIBUSB_RECIPIENT_OTHER; break;
		case 0x08: requestType = LIBUSB_REQUEST_TYPE_VENDOR | LIBUSB_RECIPIENT_DEVICE; break;
		case 0x09: requestType = LIBUSB_REQUEST_TYPE_VENDOR | LIBUSB_RECIPIENT_INTERFACE; break;
		case 0x0A: requestType = LIBUSB_REQUEST_TYPE_VENDOR | LIBUSB_RECIPIENT_ENDPOINT; break;
		case 0x0B: requestType = LIBUSB_REQUEST_TYPE_VENDOR | LIBUSB_RECIPIENT_OTHER; break;
		}

		int status = 0;
		if (req->direction)
			status = GUSBDev.value().ReadControlTransfer(requestType, req->request, req->value, req->index, outBuffer, static_cast<uint16_t>(outBufferLen));
		else
			status = GUSBDev.value().WriteControlTransfer(requestType, req->request, req->value, req->index, outBuffer, static_cast<uint16_t>(outBufferLen));

		return (status >= 0 ? ERROR_SUCCESS : ERROR_GEN_FAILURE);
	} },

	{ IOCTL_EZUSB_GET_DRIVER_VERSION, [](LPVOID inBuffer, DWORD inBufferLen, LPVOID outBuffer, DWORD outBufferLen) -> ioctl_handler_return_t {
		/*
		* IOCTL_EZUSB_GET_DRIVER_VERSION
		*/
		if (outBufferLen < sizeof(EZUSB_DRIVER_VERSION))
			return ERROR_GEN_FAILURE; //This function returns STATUS_UNSUCCESSFUL instead of invalid parameter

		PEZUSB_DRIVER_VERSION version = (PEZUSB_DRIVER_VERSION)outBuffer;
		version->MajorVersion = EZUSB_MAJOR_VERSION;
		version->MinorVersion = EZUSB_MINOR_VERSION;
		version->BuildVersion = EZUSB_BUILD_VERSION;

		return { ERROR_SUCCESS, sizeof(EZUSB_DRIVER_VERSION) };
	} },
};


DWORD TL::TranslateIOTCL(HandleManager_t::handle_t handle, DWORD dwIOCTL, LPVOID lpInBuffer, DWORD nInBufferSize,
	LPVOID lpOutBuffer, DWORD nOutBufferSize, LPDWORD lpBytesReturned)
{
	auto it = ioctl_handler_map.find(dwIOCTL);
	if (it == ioctl_handler_map.end()) {
		return ERROR_SUCCESS;
	}

	auto result = it->second(lpInBuffer, nInBufferSize, lpOutBuffer, nOutBufferSize);

	if (lpBytesReturned != NULL && result.bytesReturned.has_value()) {
		*lpBytesReturned = result.bytesReturned.value();
	}
	return result.error;
}
