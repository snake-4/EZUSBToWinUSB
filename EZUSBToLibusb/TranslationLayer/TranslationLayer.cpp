#define NOMINMAX
#include <Windows.h>
#include <vector>
#include <cstdint>
#include <unordered_map>
#include <functional>
#include <numeric>
#include <libusb-1.0/libusb.h>
#include "../EZUSB/ezusb.h"
#include "../Globals.h"
#include "TranslationLayer.h"
#include "IOCTLHandlers.h"

using namespace EZUSB;
using namespace TL;

TL::ioctl_hdl_return_t::ioctl_hdl_return_t(DWORD _error)
	: error(_error) { }
TL::ioctl_hdl_return_t::ioctl_hdl_return_t(DWORD _error, DWORD _bytesReturned)
	: error(_error), bytesReturned(_bytesReturned) { }

std::unordered_map<DWORD, ioctl_handler_t> ioctl_handler_map {

	{ IOCTL_Ezusb_GET_CURRENT_CONFIG, Hdl_IOCTL_Ezusb_GET_CURRENT_CONFIG },
	{ IOCTL_Ezusb_VENDOR_REQUEST, Hdl_IOCTL_Ezusb_VENDOR_REQUEST },
	{ IOCTL_EZUSB_BULK_READ, Hdl_IOCTL_EZUSB_BULK_READ },
	{ IOCTL_EZUSB_BULK_WRITE, Hdl_IOCTL_EZUSB_BULK_WRITE },
	{ IOCTL_EZUSB_ANCHOR_DOWNLOAD, Hdl_IOCTL_EZUSB_ANCHOR_DOWNLOAD },
	{ IOCTL_EZUSB_VENDOR_OR_CLASS_REQUEST, Hdl_IOCTL_EZUSB_VENDOR_OR_CLASS_REQUEST },
	{ IOCTL_EZUSB_GET_DRIVER_VERSION, Hdl_IOCTL_EZUSB_GET_DRIVER_VERSION },
};


DWORD TL::TranslateIOTCL(GHandleManager_t::handle_t handle, DWORD dwIOCTL, LPVOID lpInBuffer, DWORD nInBufferSize,
	LPVOID lpOutBuffer, DWORD nOutBufferSize, LPDWORD lpBytesReturned)
{
	auto it = ioctl_handler_map.find(dwIOCTL);
	if (it == ioctl_handler_map.end()) {
		return ERROR_INVALID_PARAMETER;
	}

	auto result = it->second(lpInBuffer, nInBufferSize, lpOutBuffer, nOutBufferSize);

	if (lpBytesReturned != NULL && result.bytesReturned.has_value()) {
		*lpBytesReturned = result.bytesReturned.value();
	}
	return result.error;
}
