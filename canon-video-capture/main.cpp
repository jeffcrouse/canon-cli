//
//  main.cpp
//  canon-video-capture
//
//  Created by Jeffrey Crouse on 8/18/17.
//  Copyright © 2017 See-through Lab. All rights reserved.
//


#define __MACOS__
#define ERROR_PREFIX "[error] "
#define WARNING_PREFIX "[warning] "
#define STATUS_PREFIX "[status] "
#define READY_MESSAGE "[ready]"
#define EDSDK_CHECK(X) if(X!=EDS_ERR_OK) { std::cerr << ERROR_PREFIX << Eds::getErrorString(X) << std::endl; }
#define EDSDK_MOV_FORMAT 45317
#define EDSDK_JPG_FORMAT 14337


#include <thread>
#include <unistd.h>
#import <iostream>
#include <fstream>
#import <sstream>
#import <iomanip>
#include <time.h>
#include <csignal>

#include "EDSDK.h"
#include "EDSDKErrors.h"
#include "EDSDKTypes.h"

#include "EdsStrings.h"
#include "cxxopts.hpp"


//
//  TODO:
//   - Implement max record time?
//


bool debug = false;
std::string outfile="";
bool saveToHost = false;
bool deleteAfterDownload=false;
bool listDevices = false;
bool sdkInitialized = false;
bool sessionOpen = false;


EdsInt32 cameraIndex = -1;;
EdsCameraRef camera = NULL;
EdsError err = EDS_ERR_OK;

void print_status(std::string message) {
    std::cout << STATUS_PREFIX << message << std::endl;
}

void print_warning(std::string message) {
    std::cout << WARNING_PREFIX << message << std::endl;
}

void terminate_early(std::string msg) {
    std::cerr << ERROR_PREFIX << msg << ". terminating early." << std::endl;
    if(sessionOpen) EDSDK_CHECK( EdsCloseSession(camera) )
    if(sdkInitialized) EDSDK_CHECK( EdsTerminateSDK() )
    exit(1);
}



int main(int argc, char * argv[]) {
    
    // Set the signal handler so we can tell when to stop recording
    signal(SIGINT, [](int signum) {
        std::cout << "Interrupt signal (" << signum << ") received." << std::endl;
        terminate_early("exiting");
    });
    
    
    
    
    //
    //  Parse command line arguments
    //
    try {
        cxxopts::Options options(argv[0], "Command line program to capture video from a tethered Canon camera using EDSDK");
        options.add_options()
            ("d,debug", "Enable debugging", cxxopts::value<bool>(debug))
            ("i,id", "Device ID", cxxopts::value<EdsInt32>()->default_value("0")->implicit_value("0"))
            //("s,save-to-host", "Save to Host", cxxopts::value<bool>(saveToHost))
            ("l,list-devices", "List Devices", cxxopts::value<bool>(listDevices))
            ("x,delete-after-download", "Delete files after download", cxxopts::value<bool>(deleteAfterDownload))
            ("help", "Print help")
            ;
        
        options.parse(argc, argv);
        
        if(options.count("help")) {
            std::cout << options.help() << std::endl;
            exit(0);
        }
        
        
        cameraIndex = options["id"].as<EdsInt32>();
        
    } catch (const cxxopts::OptionException& e) {
        
        std::cerr << ERROR_PREFIX << "Couldn't parse options: " << e.what() << std::endl;
        exit(1);
    }
    
    //
    // Initialize SDK
    //
    print_status("initializing SDK");
    EDSDK_CHECK( EdsInitializeSDK() );
    sdkInitialized = true;
    
    
    
    
    
    
    //
    // Check for attached cameras
    //
    EdsCameraListRef cameraList;
    UInt32 cameraCount;
    
    print_status("listing cameras");
    EDSDK_CHECK( EdsGetCameraList(&cameraList) );
    EDSDK_CHECK( EdsGetChildCount(cameraList, &cameraCount) );
    
    if(cameraCount==0) {
        terminate_early("No cameras attached.");
    }
    
    
    

    
    //
    //  List devices
    //
    if(listDevices) {
        EdsCameraRef _camera;
        EdsDeviceInfo _info;
        
        std::stringstream ss;
        ss << "Found " << cameraCount << " cameras:" << std::endl;
        ss << std::setw(10) << "Device ID";
        ss << std::setw(20) << "Description";
        ss << std::setw(10) << "Port";
        ss << std::setw(15) << "Reserved";
        ss << std::setw(30) << "Body ID" << std::endl;
        for(EdsInt32 i=0; i<cameraCount; ++i)
        {
            EDSDK_CHECK( EdsGetChildAtIndex(cameraList, i, &_camera) )
            EDSDK_CHECK( EdsGetDeviceInfo(_camera, &_info) )
            
            ss << std::setw(10) << i;
            ss << std::setw(20) << _info.szDeviceDescription;
            ss << std::setw(10) << _info.szPortName;
            ss << std::setw(15) << _info.reserved;
            
    //        EDSDK_CHECK( EdsOpenSession(_camera) )
    //        EdsDataType dataType;
    //        EdsUInt32 dataSize;
    //        EdsGetPropertySize(_camera, kEdsPropID_BodyIDEx, 0 , &dataType, &dataSize);
    //        char buf[dataSize];
    //        EDSDK_CHECK( EdsGetPropertyData(_camera, kEdsPropID_BodyIDEx, 0, dataSize, &buf) )
    //        std::string _bodyID = std::string(buf);
    //        ss << std::setw(30) << _bodyID << std::endl;
    //        EDSDK_CHECK( EdsCloseSession(_camera) )
            EDSDK_CHECK ( EdsRelease(_camera) )
        }
        std::cout << ss.str() << std::endl;

    }
    
    
    
    //
    // Get the desired camera and ssign callbacks
    //
    print_status("assigning callbacks");
    EDSDK_CHECK( EdsGetChildAtIndex(cameraList, cameraIndex, &camera) )
    

    err = EdsSetObjectEventHandler(camera, kEdsObjectEvent_All, [](EdsObjectEvent event, EdsBaseRef object, EdsVoid* context) -> EdsError EDSCALLBACK {
        if(debug)
            std::cout << "[object event] " << Eds::getObjectEventString(event) << std::endl;
        
        if(!object)
            return EDS_ERR_OK;
        
        if(event == kEdsObjectEvent_DirItemCreated) {
            std::stringstream ss;
            EdsDirectoryItemRef directoryItem = object;
            EdsDirectoryItemInfo directoryItemInfo;

            EDSDK_CHECK( EdsGetDirectoryItemInfo(directoryItem, &directoryItemInfo) )
            
            
            ss << "file size " << (directoryItemInfo.size / 1000000.0) << " mb";
            print_status( ss.str() );
            
            
            
            if(outfile.empty()) {
                time_t epoch_time = std::time(0);
                
                ss.str("canon_");
                ss << epoch_time;
                if (directoryItemInfo.format == EDSDK_MOV_FORMAT) {
                    ss << ".mp4";
                }
                else if(directoryItemInfo.format == EDSDK_JPG_FORMAT) {
                    ss << ".jpg";
                }
                else {
                    terminate_early("unknown file type");
                }
                outfile = ss.str();
                print_status("downloading "+outfile);
            }
     
            EdsStreamRef outStream;
            EDSDK_CHECK( EdsCreateFileStream(outfile.c_str(), kEdsFileCreateDisposition_CreateAlways, kEdsAccess_ReadWrite, &outStream) )
            EdsDownload(directoryItem, directoryItemInfo.size, outStream);
            EdsDownloadComplete(directoryItem);
            
            print_status("releasing data stream");
            EDSDK_CHECK( EdsRelease(outStream) )

            //
            //  Delete file after download
            //
            if(deleteAfterDownload) {
                print_status("deleting file from device");
                EDSDK_CHECK( EdsDeleteDirectoryItem(directoryItem) )
            }

            outfile = "";
            std::cout << READY_MESSAGE << std::endl;
            
        } else if(event == kEdsObjectEvent_DirItemRemoved) {
            print_status("item removed");
        } else {
            EDSDK_CHECK( EdsRelease(object) )
        }
        return EDS_ERR_OK;
    }, NULL);
    EDSDK_CHECK(err)
    
    
    err = EdsSetPropertyEventHandler(camera, kEdsPropertyEvent_All, [](EdsPropertyEvent event, EdsPropertyID propertyId, EdsUInt32 param, EdsVoid* context) -> EdsError EDSCALLBACK {
        if(debug)
            std::cout << "[property event] " << Eds::getPropertyEventString(event) << ": " << Eds::getPropertyIDString(propertyId) << " / " << param << std::endl;
        return EDS_ERR_OK;
    }, NULL);
    EDSDK_CHECK(err)
    
    
    err = EdsSetCameraStateEventHandler(camera, kEdsStateEvent_All, [](EdsStateEvent event, EdsUInt32 param, EdsVoid* context) -> EdsError EDSCALLBACK{
        if(debug)
            std::cout << "[state event] " << Eds::getStateEventString(event) << ": " << param << std::endl;
        
        if(event == kEdsStateEvent_ShutDownTimerUpdate) {
            print_status("shutdown timer extended.");
        }
        
        if(event == kEdsStateEvent_WillSoonShutDown) {
            print_status("sending keep alive");
            EdsSendStatusCommand(camera, kEdsCameraCommand_ExtendShutDownTimer, 0);        }
        
        if(event == kEdsStateEvent_Shutdown) {
            terminate_early("kEdsStateEvent_Shutdown received. Exiting.");
        }
        
        return EDS_ERR_OK;
    }, NULL);
    EDSDK_CHECK(err)
    
    
    

    
    

    

    
    
    
    //
    //  Open a session with the camera!
    //
    print_status("opening session");
    EDSDK_CHECK( EdsOpenSession(camera) )
    sessionOpen = true;
    
    
    
    
    

    
    
    
    
    //
    //  Print out serial nmber
    //
    
    EdsDataType dataType;
    EdsUInt32 dataSize;
    EdsGetPropertySize(camera, kEdsPropID_BodyIDEx, 0 , &dataType, &dataSize);
    char buf[dataSize];
    EDSDK_CHECK( EdsGetPropertyData(camera, kEdsPropID_BodyIDEx, 0, dataSize, &buf) )
    std::cout << STATUS_PREFIX << "connected to " << std::string(buf) << std::endl;
    
    
    
    
    
    
    
    //
    //  Set the save-to location
    //
    if(saveToHost) {
        print_status("setting kEdsSaveTo_Host");
        EdsUInt32 saveTo = kEdsSaveTo_Host;
        EDSDK_CHECK( EdsSetPropertyData(camera, kEdsPropID_SaveTo, 0, sizeof(saveTo) , &saveTo) )
        
        EdsCapacity capacity;// = {0x7FFFFFFF, 0x1000, 1};
        capacity.reset = 1;
        capacity.bytesPerSector = 512*8;
        capacity.numberOfFreeClusters = 36864*9999;
        EDSDK_CHECK( EdsSetCapacity(camera, capacity) )
    } else {
        print_status("setting kEdsSaveTo_Camera");
        EdsUInt32 saveTo = kEdsSaveTo_Camera;
        EDSDK_CHECK( EdsSetPropertyData(camera, kEdsPropID_SaveTo, 0, sizeof(saveTo), &saveTo) )
    }
    
    

    

    
    
    //
    //  Enter the interactive loop
    //
    std::string command;
    bool recording = false;
//    auto start = std::chrono::high_resolution_clock::now();
//    auto elapsed = std::chrono::high_resolution_clock::now() - start;
//    long milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();
    
    while (1) {
        
        CFRunLoopRunInMode( kCFRunLoopDefaultMode, 0, false); // https://stackoverflow.com/questions/23472376/canon-edsdk-handler-isnt-called-on-mac
        
        if (isatty(STDIN_FILENO)){
            std::cout << "> ";
        }
        std::getline(std::cin, command);
        
        std::vector<std::string> words;
        std::istringstream iss(command);
        for(std::string s; iss >> s;) words.push_back(s);
        
        
        if(words[0].compare("record")==0) {
            if(recording) {
                print_warning("already recording");
            } else {
                outfile = "";
                if(words.size() > 1) {
                    outfile = words[1];
                }
                print_status("start recording");
                EdsUInt32 record_start = 4; // Begin movie shooting
                EDSDK_CHECK( EdsSetPropertyData(camera, kEdsPropID_Record, 0, sizeof(record_start), &record_start) )
                recording = true;
            }
        }
        
        
        else if(words[0].compare("stop")==0) {
            if(!recording) {
                print_warning("not recording");
            } else {
                if(words.size() > 1) {
                    outfile = words[1];
                }
                
                print_status("stopping");
                EdsUInt32 record_stop = 0; // End movie shooting
                EDSDK_CHECK( EdsSetPropertyData(camera, kEdsPropID_Record, 0, sizeof(record_stop), &record_stop) )
                recording = false;
            }
        }
        
        else if(words[0].compare("picture")==0) {
            print_status("not yet implemented");
            
            /*
            outfile = "";
            if(words.size() > 1) {
                outfile = words[1];
            }
            EDSDK_CHECK( EdsSendCommand(camera, kEdsCameraCommand_TakePicture, 0) )
             */
        }

        
        else if (command.compare("exit") == 0) {
            print_status("exit");
            break;
        }
        else {
            print_warning("unknown command");
        }
        
        std::cout << READY_MESSAGE << std::endl;
    }
    
    
    

    
    //
    //  Terminate SDK
    //
    print_status("terminating SDK");
    EDSDK_CHECK( EdsRelease(cameraList) )
    if(sessionOpen) EDSDK_CHECK( EdsCloseSession(camera) )
    sessionOpen = false;
    if(sdkInitialized) EDSDK_CHECK( EdsTerminateSDK() )
    sdkInitialized = false;
    

    print_status("done!");
    return 0;
}
