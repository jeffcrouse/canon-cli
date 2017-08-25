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
#define EDSDK_CHECK(X) if(X!=EDS_ERR_OK) { std::cerr << ERROR_PREFIX << Eds::getErrorString(X) << std::endl; }
#define EDSDK_MOV_FORMAT 45317
#define EDSDK_JPG_FORMAT 14337

#include <sys/stat.h>
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
std::vector<std::string> command_queue;
std::mutex command_queue_mutex;
bool sigint = false;

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

bool fileExists(const std::string& filename) {
    struct stat buf;
    if (stat(filename.c_str(), &buf) != -1) return true;
    return false;
}


int main(int argc, char * argv[]) {
    
    // Set the signal handler so we can tell when to stop recording
    signal(SIGINT, [](int signum) {
        std::cout << "Interrupt signal (" << signum << ") received." << std::endl;
        if(sigint) exit(1);
        sigint = true;
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
    
    EDSDK_CHECK( EdsGetCameraList(&cameraList) );
    EDSDK_CHECK( EdsGetChildCount(cameraList, &cameraCount) );
    
    if(cameraCount==0) {
        terminate_early("No cameras attached.");
    }
    
    
    

    
    //
    //  List devices
    //
    if(listDevices) {
        print_status("listing devices");
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
                
                ss.str("");
                ss << "canon_" << cameraIndex << "_"  << epoch_time;

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
                
                if(fileExists(outfile)) {
                    terminate_early(outfile+" already exists");
                }
                
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

            print_status("downloaded "+outfile);
            
            outfile = "";
    
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
    
    
    err = EdsSetCameraStateEventHandler(camera, kEdsStateEvent_All, [](EdsStateEvent event, EdsUInt32 param, EdsVoid* context) -> EdsError EDSCALLBACK {
        if(debug)
            std::cout << "[state event] " << Eds::getStateEventString(event) << ": " << param << std::endl;
        
        if(event == kEdsStateEvent_ShutDownTimerUpdate) {
            print_status("shutdown timer extended.");
        }
        
        if(event == kEdsStateEvent_WillSoonShutDown) {
            print_status("sending keep alive");
            EdsSendStatusCommand(camera, kEdsCameraCommand_ExtendShutDownTimer, 0);
        }
        
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
    //  Input thread
    //
    std::thread input([](){
        while(!sigint) {
//            if (isatty(STDIN_FILENO)){
//                std::cout << "> ";
//            }
            
            std::string command;
            std::getline(std::cin, command);
            
            if (command.compare("exit") == 0) {
                print_status("exit");
                sigint = true;
            } else {
                command_queue_mutex.lock();
                command_queue.push_back(command);
                command_queue_mutex.unlock();
            }
        }
    });
    
    
    //
    //  Enter the interactive loop
    //
    
    auto start = std::chrono::high_resolution_clock::now();
    while (!sigint) {
        CFRunLoopRunInMode( kCFRunLoopDefaultMode, 0, false); // https://stackoverflow.com/questions/23472376/canon-edsdk-handler-isnt-called-on-mac
        
        
        EDSDK_CHECK( EdsGetEvent() ) // I don't think this dos anything.
        
        //
        //  Process any commands in the user input queue
        //
        if(command_queue.size()) {
            
            // Get the recording state.
            EdsUInt32 recordStart;
            EDSDK_CHECK( EdsGetPropertyData(camera, kEdsPropID_Record, 0, sizeof(recordStart), &recordStart) )
            bool recording = (recordStart==4);
            
            
            command_queue_mutex.lock();
            std::string command = command_queue.back();
            command_queue.pop_back();
            command_queue_mutex.unlock();
            
            
            command.erase( remove( command.begin(), command.end(), '\"' ), command.end() );
            
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
               // print_status("not yet implemented");
                
 
                outfile = "";
                if(words.size() > 1) {
                    outfile = words[1];
                }
                EDSDK_CHECK( EdsSendCommand(camera, kEdsCameraCommand_TakePicture, 0) )
            }

            else {
                print_warning("unknown command: "+words[0]);
            }
        
        }
        
        
        auto now = std::chrono::high_resolution_clock::now();
        auto elapsed = now - start;
        long seconds = std::chrono::duration_cast<std::chrono::seconds>(elapsed).count();
        if(seconds > 60) {
            //print_status("sending keep alive");
            EdsSendStatusCommand(camera, kEdsCameraCommand_ExtendShutDownTimer, 0);
            start = std::chrono::high_resolution_clock::now();
        }
        
        
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    
    
    
    
    
    //
    //  Terminate SDK
    //
    print_status("waiting for input thread");
    input.join();
    
    print_status("terminating SDK");
    EDSDK_CHECK( EdsRelease(cameraList) )
    if(sessionOpen) EDSDK_CHECK( EdsCloseSession(camera) )
    sessionOpen = false;
    if(sdkInitialized) EDSDK_CHECK( EdsTerminateSDK() )
    sdkInitialized = false;
    

    print_status("done!");
    return 0;
}
