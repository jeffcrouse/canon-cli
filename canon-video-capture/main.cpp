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
//



#define __MACOS__
#define ERROR_PREFIX "[error] "
#define STATUS_PREFIX "[status] "
#define EDSDK_CHECK(X) if(X!=EDS_ERR_OK) { std::cerr << ERROR_PREFIX << Eds::getErrorString(X) << std::endl;  exit(1); }
#define EDSDK_MOV_FORMAT 45317

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


bool sendKeepAlive = false;
bool debug = false;
std::string outfile="";
int camera_id = 0;
bool saveToHost = false;
bool sigint_received = false;
bool deleteAfterDownload = true;
bool listDevices = false;
bool keepAliveOnly = false;

long dirItemTimeout = 10000;
long maxRecordTime = 600000;


EdsCameraRef camera;
EdsError err;
EdsDirectoryItemRef directoryItem = NULL;
EdsDirectoryItemInfo directoryItemInfo;

void print_status(std::string message) {
    std::cout << STATUS_PREFIX << message << std::endl;
}

std::string make_default_filename() {
    time_t epoch_time = std::time(0);
    std::stringstream ss;
    ss << "vid_" << epoch_time << ".mp4";
    return ss.str();
}

void terminate_early(std::string msg) {
    std::cerr << ERROR_PREFIX << msg << ". terminating early." << std::endl;
    EDSDK_CHECK( EdsCloseSession(camera) )
    EDSDK_CHECK( EdsTerminateSDK() )
    exit(1);
}


int main(int argc, char * argv[]) {
    
    // Set the signal handler so we can tell when to stop recording
    signal(SIGINT, [](int signum) {
        std::cout << "Interrupt signal (" << signum << ") received." << std::endl;
        if(sigint_received) exit(1);
        else sigint_received = true;
    });
    
    
    
    
    //
    //  Parse command line arguments
    //
    try {
        cxxopts::Options options(argv[0], "Command line program to capture video from a tethered Canon camera using EDSDK");
        options.add_options()
            ("d,debug", "Enable debugging", cxxopts::value<bool>(debug))
            ("i,id", "Camera ID", cxxopts::value<int>(), "0")
            ("o,outfile", "Out file to save to", cxxopts::value<std::string>()->default_value(make_default_filename()))
            //("s,save-to-host", "Save to Host", cxxopts::value<bool>(saveToHost))
            ("l,list-devices", "List Devices", cxxopts::value<bool>(listDevices))
            ("k,keep-alive", "Send Keep Alive message", cxxopts::value<bool>(keepAliveOnly))
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
        terminate_early("No cameras attached.");
    }
    
    
    
    
    //
    //  List devices
    //
    std::stringstream ss;
    ss << "Found " << cameraCount << " cameras:" << std::endl;
    ss << std::setw(10) << "Device ID";
    ss << std::setw(25) << "Description";
    ss << std::setw(6) << "Port ";
    ss << std::setw(11) << "Reserved" << std::endl;
    
    for(EdsInt32 i=0; i<cameraCount; ++i)
    {
        
        EdsCameraRef _camera;
        EdsDeviceInfo _info;
        
        EDSDK_CHECK( EdsGetChildAtIndex(cameraList, i, &_camera) )
        EDSDK_CHECK( EdsGetDeviceInfo(_camera, &_info) )
        //EDSDK_CHECK( EdsOpenSession(_camera) )
        
        ss << std::setw(10) << i;
        ss << std::setw(25) << _info.szDeviceDescription;
        ss << std::setw(6) << _info.szPortName;
        ss << std::setw(11) << _info.reserved << std::endl;
        
//        EdsDataType dataType;
//        EdsUInt32 dataSize;
//        EdsGetPropertySize(_camera, kEdsPropID_BodyIDEx, 0 , &dataType, &dataSize);
//        char buf[dataSize];
//        EDSDK_CHECK( EdsGetPropertyData(_camera, kEdsPropID_BodyIDEx, 0, dataSize, &buf) )
//        ss << std::string(buf) << std::endl;
//        
//        EDSDK_CHECK( EdsCloseSession(_camera) )
        EDSDK_CHECK ( EdsRelease(_camera) )
    }
    std::cout << ss.str() << std::endl;
    
    

    
    
    //
    // Get the desired camera and ssign callbacks
    //
    print_status("assigning callbacks");
    EdsInt32 cameraIndex = camera_id;
    EDSDK_CHECK( EdsGetChildAtIndex(cameraList, cameraIndex, &camera) )
    

    err = EdsSetObjectEventHandler(camera, kEdsObjectEvent_All, [](EdsObjectEvent event, EdsBaseRef object, EdsVoid* context) -> EdsError EDSCALLBACK {
        if(debug)
            std::cout << "[object event] " << Eds::getObjectEventString(event) << std::endl;
        
        if(!object) return EDS_ERR_OK;
        
        if(event == kEdsObjectEvent_DirItemCreated) {
            directoryItem = object;
            EDSDK_CHECK( EdsGetDirectoryItemInfo(directoryItem, &directoryItemInfo) )
            
        } else if(event == kEdsObjectEvent_DirItemRemoved) {
            // no need to release a removed item
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
            sendKeepAlive = true;
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
    
    
    
    
    
    

    
    
    
    
    //
    //  Print out serial nmber
    //
    
    EdsDataType dataType;
    EdsUInt32 dataSize;
    EdsGetPropertySize(camera, kEdsPropID_BodyIDEx, 0 , &dataType, &dataSize);
    char buf[dataSize];
    EDSDK_CHECK( EdsGetPropertyData(camera, kEdsPropID_BodyIDEx, 0, dataSize, &buf) )
    std::cout << STATUS_PREFIX << "connected to " << std::string(buf) << std::endl;
    
    
    
    
    
    
    
    if(keepAliveOnly) {
        print_status("sending keep alive");
        EdsSendStatusCommand(camera, kEdsCameraCommand_ExtendShutDownTimer, 0);
        
 
        std::this_thread::sleep_for (std::chrono::milliseconds(1000));

        
        print_status("shutting down");
        EDSDK_CHECK( EdsCloseSession(camera) )
        EDSDK_CHECK( EdsTerminateSDK() )
        exit(0);
    }
    
    
    
    
    
    
    
    
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
    //  Start recording
    //
    print_status("start recording");
    EdsUInt32 record_start = 4; // Begin movie shooting
    EDSDK_CHECK( EdsSetPropertyData(camera, kEdsPropID_Record, 0, sizeof(record_start), &record_start) )
    
    
    
    
    auto start = std::chrono::high_resolution_clock::now();
    auto elapsed = std::chrono::high_resolution_clock::now() - start;
    long milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();
    
    
    //
    //  Wait for SIGINT to stop recording
    //
    print_status("recording");
    while(sigint_received==false && milliseconds < maxRecordTime)
    {
        if(sendKeepAlive) {
            EDSDK_CHECK( EdsSendStatusCommand(camera, kEdsCameraCommand_ExtendShutDownTimer, 0) )
            sendKeepAlive = false;
        }
        
        std::this_thread::sleep_for (std::chrono::milliseconds(100));
        elapsed = std::chrono::high_resolution_clock::now() - start;
        milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();
    }
    
    if(milliseconds > maxRecordTime) {
        print_status("maximum recording time exceeded");
    }
    
    
    
    
    
    //
    //  Stop recording!
    //
    print_status("stopping");
    directoryItem = NULL;               // This is to make sure we get the right dirItem when we are done downloading.
    EdsUInt32 record_stop = 0; // End movie shooting
    EDSDK_CHECK( EdsSetPropertyData(camera, kEdsPropID_Record, 0, sizeof(record_stop), &record_stop) )
    
    
    
    
    
    
    //
    //  Wait for camera to deliver directoryItem to EdsSetObjectEventHandler
    //
    print_status("waiting for directory item");
    start = std::chrono::high_resolution_clock::now();
    elapsed = std::chrono::high_resolution_clock::now() - start;
    milliseconds = 0;
    while(directoryItem == NULL && milliseconds < dirItemTimeout)
    {
        CFRunLoopRunInMode( kCFRunLoopDefaultMode, 0, false); // https://stackoverflow.com/questions/23472376/canon-edsdk-handler-isnt-called-on-mac
        
        EDSDK_CHECK( EdsGetEvent() )
        
        if(sendKeepAlive) {
            EDSDK_CHECK( EdsSendStatusCommand(camera, kEdsCameraCommand_ExtendShutDownTimer, 0) )
            sendKeepAlive = false;
        }
        
        std::this_thread::sleep_for (std::chrono::milliseconds(100));
        
        elapsed = std::chrono::high_resolution_clock::now() - start;
        milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();
    }
    


    
    
    if(directoryItem==NULL) {
        terminate_early("directory item timed out.");
    }





    //
    //  Get the Directory Item Info
    //
    print_status("fetching directory item info");
    
    EdsDirectoryItemInfo dirItemInfo;
    EDSDK_CHECK( EdsGetDirectoryItemInfo(directoryItem, &dirItemInfo) )
    
    if (dirItemInfo.format != EDSDK_MOV_FORMAT) {
        terminate_early("file format is not the video format");
    }
    
    
    
    
    //
    // Download the video to the EdsStreamRef
    //
    print_status("downloading video");
    
    
    EdsStreamRef outStream;
    EDSDK_CHECK( EdsCreateFileStream(outfile.c_str(), kEdsFileCreateDisposition_CreateAlways, kEdsAccess_ReadWrite, &outStream) )
    EdsDownload(directoryItem, directoryItemInfo.size, outStream);
    EdsDownloadComplete(directoryItem);
    
 
    
    
    
    
    
    
    
    print_status("releasing data stream");
    EDSDK_CHECK( EdsRelease(outStream) )
    
    
    
    

    
    /**
    
     EdsStreamRef stream = NULL;
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
        std::cerr << "WARNING: File has bad byte" << std::endl;
    }
    
    
    
    
    
    //
    //  Release the Stream!
    //
    print_status("releasing data stream");
    EDSDK_CHECK( EdsRelease(stream) )
    */
    
    
    
    
    
    //
    //  Delete file after download
    //
    if(deleteAfterDownload) {
        print_status("deleting file from device");
        EDSDK_CHECK( EdsDeleteDirectoryItem(directoryItem) )
    }


    
    
    //
    //  We've listed and assigned callbacks, so we don't need this list any more.
    //
    EDSDK_CHECK( EdsRelease(cameraList) )
    
    
    
    
    //
    //  Terminate SDK
    //
    print_status("terminating SDK");
    EDSDK_CHECK( EdsCloseSession(camera) )
    EDSDK_CHECK( EdsTerminateSDK() )

    

    print_status("done!");
    return 0;
}
