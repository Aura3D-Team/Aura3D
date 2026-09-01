#include "aura/Core/AuraException/AuraException.h"

namespace aura3d {

AuraException::AuraException() noexcept
    : std::exception()
{
}

AuraException::AuraException(const char* msg)
    : _msg(msg)
{
}

AuraException::AuraException(const std::string& msg)
    : _msg(msg.c_str())
{
}

#ifdef AURA_HAS_VULKAN
AuraException::AuraException(const VkResult code)
{
    if (vkResultToString.find(code) != vkResultToString.end()) {
        _msg = vkResultToString.at(code);
    } else {
        _msg = "VkResult not mapped.";
    }
}
#endif

AuraException::~AuraException() noexcept = default;

const char* AuraException::what() const noexcept
{
    return _msg.c_str();
}

}
