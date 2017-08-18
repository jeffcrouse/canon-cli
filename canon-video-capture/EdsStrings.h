#pragma once

#define __MACOS__

#include <string>
#include "EDSDK.h"
#include "EDSDKErrors.h"
#include "EDSDKTypes.h"

namespace Eds {
	std::string getErrorString(EdsError error);
	std::string getPropertyIDString(EdsPropertyID property);
	std::string getPropertyEventString(EdsPropertyEvent event);
	std::string getObjectEventString(EdsObjectEvent event);
	std::string getStateEventString(EdsStateEvent event);
}
