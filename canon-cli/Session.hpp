//
//  CCSession.hpp
//  canon-video-capture
//
//  Created by Jeffrey Crouse on 9/11/17.
//  Copyright © 2017 See-through Lab. All rights reserved.
//

#pragma once

#define __MACOS__
#define EDSDK_CHECK(X) if(X!=EDS_ERR_OK) { throw std::runtime_error(Eds::getErrorString(X)); }
#define EDSDK_MOV_FORMAT 45317
#define EDSDK_JPG_FORMAT 14337

#include <sys/stat.h>
#include <vector>
#include <exception>
#include "Logger.hpp"

#include "EDSDK.h"
#include "EDSDKErrors.h"
#include "EDSDKTypes.h"
#include "EdsStrings.h"



namespace cc {
    
    typedef std::vector<std::string> command;
    
    typedef std::chrono::high_resolution_clock::time_point time_point;
    typedef std::chrono::high_resolution_clock high_resolution_clock;
    typedef std::chrono::milliseconds milliseconds;

    
    class Session {
        
    private:
        static Session* instance;
        Session();
        EdsCameraListRef cameraList;
        UInt32 cameraCount;
        bool sdkInitialized;
        EdsCameraRef camera = NULL;
        bool sessionOpen = false;
        std::string outfile;
        std::vector<command> command_queue;
        std::mutex command_queue_mutex;
        
        
        time_point start;
        time_point next_keepalive;
        //time_point next_status;
        
    public:
        
        static Session* getInstance();
        
        ~Session();
        
        void updateCameraList();
        
        std::string getDevicesAsJSON();
        
        EdsError download(EdsBaseRef object);
        EdsError EDSCALLBACK handleEvent(EdsObjectEvent event, EdsBaseRef object);
        EdsError EDSCALLBACK handleProperty(EdsPropertyEvent event, EdsPropertyID propertyId, EdsUInt32 param);
        EdsError EDSCALLBACK handleState(EdsStateEvent event, EdsUInt32 param);
        
        void open();
        void process();
        bool isRecording();
        
        std::string getSerial();
        
        void addCommand(command cmd) {
           command_queue_mutex.lock();
           command_queue.push_back(cmd);
           command_queue_mutex.unlock();
        }

        static bool fileExists(const std::string& filename) {
            struct stat buf;
            if (stat(filename.c_str(), &buf) != -1) return true;
            return false;
        }

        int maxDuration;
        bool downloading;
        bool deleteAfterDownload;
        bool saveToHost;
        bool overwrite;
        bool canceled = false;
        EdsInt32 cameraIndex;
        std::string defaultDir;
    };
    

}



