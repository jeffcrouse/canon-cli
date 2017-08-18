//
//  main.cpp
//  canon-video-capture
//
//  Created by Jeffrey Crouse on 8/18/17.
//  Copyright © 2017 See-through Lab. All rights reserved.
//


//
// TO DO
// - Connect to camera based on BodyID?
// - Do we need to transfer, save, and delete file if we are saving to host?
//


#define __MACOS__
#define ERROR_PREFIX "[erorr] "
#define STATUS_PREFIX "[status] "
#define EDSDK_CHECK(X) if(X!=EDS_ERR_OK) { std::cerr << ERROR_PREFIX << Eds::getErrorString(X) << std::endl;  exit(1); } \

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


bool sendKeepAlive = false;
bool debug = false;
std::string outfile="";
int camera_id = 0;
bool saveToHost = true;
bool sigint_received = false;
bool deleteAfterDownload = true;
bool listDevices = false;
EdsError err;
EdsDirectoryItemRef directoryItem = NULL;
unsigned int microseconds = 50;

struct EdsDevice {
    int id;
    EdsCameraRef camera;
    EdsDeviceInfo info;
    std::string BodyIDEx;
};

void print_status(std::string message) {
    std::cout << STATUS_PREFIX << message;
}

std::string make_default_filename() {
    time_t epoch_time = std::time(0);
    std::stringstream ss;
    ss << "vid_" << epoch_time << ".mp4";
    return ss.str();
}

void signal_handler( int signum ) {
    std::cout << "Interrupt signal (" << signum << ") received." << std::endl;
    sigint_received = true;
}

void terminate_early() {
    EDSDK_CHECK( EdsTerminateSDK() )
    exit(1);
}

int main(int argc, char * argv[]) {
    signal(SIGINT, signal_handler);
    
    
    //
    //  Parse command line arguments
    //
    try {
        cxxopts::Options options(argv[0], "Command line program to capture video from a tethered Canon camera using EDSDK");
        options.add_options()
            ("d,debug", "Enable debugging", cxxopts::value<bool>(debug))
            ("i,id", "Camera ID", cxxopts::value<int>(), "0")
            ("o,outfile", "Out file", cxxopts::value<std::string>()->default_value(make_default_filename()))
            ("s,save-to", "Save to", cxxopts::value<std::string>()->default_value("device"))
            ("l,list-devices", "List Devices", cxxopts::value<bool>(listDevices))
            ;
        
        options.parse(argc, argv);
        
        camera_id = options["id"].as<int>();
        outfile = options["outfile"].as<std::string>();
        
        if(debug) {
            std::cout << "camera ID = " << camera_id << std::endl;
            std::cout << "outfile = " << outfile << std::endl;
        }
        
    } catch (const cxxopts::OptionException& e) {
        
        std::cerr << ERROR_PREFIX << "Couldn't parse options: " << e.what() << std::endl;
        exit(1);
    }
    

    
    //
    // Initialize SDK
    //
    print_status("initializing SDK");
    EDSDK_CHECK( EdsInitializeSDK() );

    
    
    
    //
    // Check for attached cameras
    //
    EdsCameraListRef cameraList;
    UInt32 cameraCount;
    
    print_status("listing cameras");
    EDSDK_CHECK( EdsGetCameraList(&cameraList) );
    EDSDK_CHECK( EdsGetChildCount(cameraList, &cameraCount) );
    
    if(cameraCount==0) {
        std::cerr << ERROR_PREFIX << "No cameras attached." << std::endl;
        terminate_early();
    }
    
    
    
    
    //
    //  List devices (int id, EdsCameraRef, EdsGetDeviceInfo, string BodyIDEx)
    //
    std::map<int,EdsDevice> devices;

    for(EdsInt32 i=0; i<cameraCount; ++i)
    {
        EdsDevice device;
        device.id = i;
        
        EDSDK_CHECK( EdsGetChildAtIndex(cameraList, i, &device.camera) )
        EDSDK_CHECK( EdsGetDeviceInfo(device.camera, &device.info) )
    
        
        EdsDataType dataType;
        EdsUInt32 dataSize;
        EdsGetPropertySize(device.camera, kEdsPropID_BodyIDEx, 0 , &dataType, &dataSize);
        char buf[dataSize];
        EDSDK_CHECK( EdsGetPropertyData(device.camera, kEdsPropID_BodyIDEx, 0, dataSize, &buf) )

        device.BodyIDEx = std::string(buf);
        devices.insert(std::make_pair( i, device));
        
        //EDSDK_CHECK ( EdsRelease(camera) )
    }
    
    EDSDK_CHECK( EdsRelease(cameraList) )
    

    
    
    
    //
    //  Iterate through the device map if requested.
    //
    if(listDevices)
    {
        std::stringstream ss;
        ss << "Found " << devices.size() << " cameras:" << std::endl;
        ss << std::setw(10) << "Device ID";
        ss << std::setw(25) << "Description";
        ss << std::setw(6) << "Port ";
        ss << std::setw(11) << "Reserved";
        ss << std::setw(20) << "Serial" << std::endl;
        
        for (auto const& device : devices)
        {
            ss << std::setw(10) << device.second.id;
            ss << std::setw(25) << device.second.info.szDeviceDescription;
            ss << std::setw(6) << device.second.info.szPortName;
            ss << std::setw(11) << device.second.info.reserved;
            ss << std::setw(20) << device.first << std::endl;
        }
        
        std::cout << ss.str();
        
        terminate_early();
    }
    
    
    
    
    //
    // Connect to specified device by assigning event handlers
    //
    print_status("assigning callbacks");
    EdsCameraRef camera;
    EdsInt32 cameraIndex = camera_id;
    EDSDK_CHECK( EdsGetChildAtIndex(cameraList, cameraIndex, &camera) )
    
    
    err = EdsSetObjectEventHandler(camera, kEdsObjectEvent_All, [](EdsObjectEvent event, EdsBaseRef object, EdsVoid* context) -> EdsError EDSCALLBACK {
        std::cout << "[object event] " << Eds::getObjectEventString(event);
        if(!object) return EDS_ERR_OK;
        
        if(event == kEdsObjectEvent_DirItemCreated) {
            directoryItem = object;
        } else if(event == kEdsObjectEvent_DirItemRemoved) {
            // no need to release a removed item
        } else {
            EDSDK_CHECK( EdsRelease(object) )
        }
        return EDS_ERR_OK;
    }, NULL);
    EDSDK_CHECK(err)
    
    err = EdsSetPropertyEventHandler(camera, kEdsPropertyEvent_All, [](EdsPropertyEvent event, EdsPropertyID propertyId, EdsUInt32 param, EdsVoid* context) -> EdsError EDSCALLBACK {
        std::cout << "[property event] " << Eds::getPropertyEventString(event) << ": " << Eds::getPropertyIDString(propertyId) << " / " << param;
        return EDS_ERR_OK;
    }, NULL);
    EDSDK_CHECK(err)
    
    err = EdsSetCameraStateEventHandler(camera, kEdsStateEvent_All, [](EdsStateEvent event, EdsUInt32 param, EdsVoid* context) -> EdsError EDSCALLBACK {
        std::cout << "[state event] " << Eds::getStateEventString(event) << ": " << param;
        
        if(event == kEdsStateEvent_WillSoonShutDown) {
            sendKeepAlive = true;
        }

        if(event == kEdsStateEvent_Shutdown) {
            print_status("kEdsStateEvent_Shutdown received. Exiting.");
            terminate_early();
        }
        
        return EDS_ERR_OK;
    }, NULL);
    EDSDK_CHECK(err)

    
    
    
    

    
    
    
    
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
        EDSDK_CHECK( EdsSetPropertyData(camera, kEdsPropID_SaveTo, 0, sizeof(saveTo) , &saveTo) )
    }
    
    
    
    
    //
    //  Start recording
    //
    print_status("start recording");
    EdsUInt32 record_start = 4; // Begin movie shooting
    EDSDK_CHECK( EdsSetPropertyData(camera, kEdsPropID_Record, 0, sizeof(record_start), &record_start) )
    
    
    
    
    //
    //  Wait for SIGINT to stop recording
    //
    print_status("recording");
    while(!sigint_received){
        std::cout << ".";
        usleep(microseconds);
        
        if(sendKeepAlive) {
            EDSDK_CHECK( EdsSendStatusCommand(camera, kEdsCameraCommand_ExtendShutDownTimer, 0) )
            sendKeepAlive = false;
        }
    }
    std::cout  << std::endl;
    
    
    
    
    //
    //  Stop recording!
    //
    print_status("stopping");
    EdsUInt32 record_stop = 0; // End movie shooting
    EDSDK_CHECK( EdsSetPropertyData(camera, kEdsPropID_Record, 0, sizeof(record_stop), &record_stop) )
    
    
    
    
    
    
    //
    //  Wait for camera to deliver directoryItem to EdsSetObjectEventHandler
    //
    print_status("waiting for directory item");
    while(!directoryItem) {
        std::cout << ".";
        usleep(microseconds);
    }
    std::cout << std::endl;
   
    
    
    
    
    
    //
    //  Get the Directory Item Info
    //
    print_status("fetching directory item info");
    EdsStreamRef stream = NULL;
    EdsDirectoryItemInfo dirItemInfo;
    EDSDK_CHECK( EdsGetDirectoryItemInfo(directoryItem, &dirItemInfo) )
    
    
    //
    // Download the video to the EdsStreamRef
    //
    print_status("download video");
    EDSDK_CHECK( EdsCreateMemoryStream(0, &stream) )
    EDSDK_CHECK( EdsDownload(directoryItem, dirItemInfo.size, stream) )
    EDSDK_CHECK( EdsDownloadComplete(directoryItem) )
    
    
    
    //
    //  Get pointer to the stream
    //
    print_status("fetching poiter to data");
    EdsUInt64 length;
    EdsGetLength(stream, &length);
    char* streamPointer;
    EDSDK_CHECK( EdsGetPointer(stream, (EdsVoid**)&streamPointer) )
    EDSDK_CHECK( EdsGetLength(stream, &length) )

    
    

    //
    //  Save buffer to disk!
    //
    print_status("saving data to disk");
    std::ofstream fout(outfile, std::ios::out | std::ios::binary);
    fout.write((char*)&streamPointer[0], length);
    fout.close();
    if(fout.bad()) {
        std::cout << "File is bad. Exiting" << std::endl;
        terminate_early();
    }
    
    
    //
    //  Release the Stream!
    //
    EDSDK_CHECK( EdsRelease(stream) )
    
    
    
    //
    //  Delete file after download
    //
    if(deleteAfterDownload) {
        print_status("deleting file");
        EDSDK_CHECK( EdsDeleteDirectoryItem(directoryItem) )
    }
    
    
    
    
    //
    //  Terminate SDK
    //
    print_status("terminating SDK");
    EDSDK_CHECK( EdsTerminateSDK() )

    
    print_status("done!");
    return 0;
}
