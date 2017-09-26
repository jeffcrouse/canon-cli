//
//  Logger.hpp
//  canon-video-capture
//
//  Created by Jeffrey Crouse on 9/11/17.
//  Copyright © 2017 See-through Lab. All rights reserved.
//

#pragma once

#include <iostream>
#include <string>

#define LOG_VERBOSE 0
#define LOG_STATUS 1
#define LOG_WARNING 2
#define LOG_ERROR 3

namespace cc {
    class Logger {
    private:
        static Logger* instance;
        Logger() : level(LOG_WARNING) {
        
        }
        
    public:
        
        static Logger* getInstance();
        
        int level;
        void verbose(std::string message);
        void status(std::string message);
        void warning(std::string message);
        void error(std::string message);
    };
}
