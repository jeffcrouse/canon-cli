//
//  Logger.cpp
//  canon-video-capture
//
//  Created by Jeffrey Crouse on 9/11/17.
//  Copyright © 2017 See-through Lab. All rights reserved.
//

#include "Logger.hpp"

cc::Logger* cc::Logger::instance = 0;

namespace cc {
    
    Logger* Logger::getInstance() {
        if (instance == 0) instance = new Logger();
        return instance;
    }

    void Logger::verbose(std::string message) {
        if(level <= LOG_VERBOSE) std::cout << "[verbose] " << message << std::endl;
    }
    
    void Logger::status(std::string message)    {
        if(level <= LOG_STATUS) std::cout << "[status] " << message << std::endl;
    }
    
    void Logger::warning(std::string message)   {
        if(level <= LOG_WARNING) std::cout << "[warning] " << message << std::endl;
    }
    
    void Logger::error(std::string message)     {
        if(level <= LOG_ERROR) std::cout << "[error] " << message << std::endl;
    }
}
