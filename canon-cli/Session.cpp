//
//  Session.cpp
//  canon-video-capture
//
//  Created by Jeffrey Crouse on 9/11/17.
//  Copyright © 2017 See-through Lab. All rights reserved.
//

#include "Session.hpp"
#include "json.hpp"

using json = nlohmann::json;

namespace cc {

    Session* Session::instance = 0;
    
    // ----------------------------------------------------------------------
    Session::Session() :
    sdkInitialized(false),
    deleteAfterDownload(false),
    saveToHost(false),
    overwrite(false),
    cameraIndex(-1),
    maxDuration(-1),
    sessionOpen(false),
    downloading(false) {
        cc::Logger::getInstance()->status("initializing SDK");
        EDSDK_CHECK( EdsInitializeSDK() );
        sdkInitialized = true;
        
        start = std::chrono::high_resolution_clock::now();
        next_keepalive = start + std::chrono::seconds(60);
    }

    // ----------------------------------------------------------------------
    Session::~Session() {
        Logger::getInstance()->status("ending session");
        if(sessionOpen) EDSDK_CHECK( EdsCloseSession(camera) )
        sessionOpen = false;
        
       // Logger::getInstance()->status("releasing camera list");
       // if(cameraList) EDSDK_CHECK( EdsRelease(cameraList) )
        
        Logger::getInstance()->status("terminating SDK");
        if(sdkInitialized) EDSDK_CHECK( EdsTerminateSDK() )
        sdkInitialized = false;
    }
    

    // ----------------------------------------------------------------------
    Session* Session::getInstance() {
        if (instance == 0) {
            instance = new Session();
        }
        return instance;
    }

    // ----------------------------------------------------------------------
    std::string Session::getDevicesAsJSON() {
        updateCameraList();
        
        EdsCameraRef _camera;
        EdsDeviceInfo _info;
        json j = json::array();
        for(EdsInt32 i=0; i<cameraCount; ++i)
        {
            EDSDK_CHECK( EdsGetChildAtIndex(cameraList, i, &_camera) )
            EDSDK_CHECK( EdsGetDeviceInfo(_camera, &_info) )
            j[i]["description"] =  _info.szDeviceDescription;
            j[i]["port"] = _info.szPortName;
            j[i]["reserved"] = _info.reserved;
            
            EDSDK_CHECK( EdsOpenSession(_camera) )
            EdsDataType dataType;
            EdsUInt32 dataSize;
            EdsGetPropertySize(_camera, kEdsPropID_BodyIDEx, 0 , &dataType, &dataSize);
            char buf[dataSize];
            EDSDK_CHECK( EdsGetPropertyData(_camera, kEdsPropID_BodyIDEx, 0, dataSize, &buf) )
            j[i]["body"] = std::string(buf);
            EDSDK_CHECK( EdsCloseSession(_camera) )
            
            EDSDK_CHECK ( EdsRelease(_camera) )
        }
        return j.dump(4);
    }

    // ----------------------------------------------------------------------
    EdsError Session::download(EdsBaseRef object) {
        downloading = true;
        
        EdsDirectoryItemRef directoryItem = object;
        EdsDirectoryItemInfo directoryItemInfo;
        
        EDSDK_CHECK( EdsGetDirectoryItemInfo(directoryItem, &directoryItemInfo) )
        
        std::stringstream ss;
        ss << "file size " << (directoryItemInfo.size / 1000000.0) << " mb";
        cc::Logger::getInstance()->status(ss.str());
        
        if(outfile.empty()) {
            time_t epoch_time = std::time(0);
            
            ss.str("");
            ss << defaultDir << "/canon_" << cameraIndex << "_"  << epoch_time;
            
            if (directoryItemInfo.format == EDSDK_MOV_FORMAT) {
                ss << ".mp4";
            }
            else if(directoryItemInfo.format == EDSDK_JPG_FORMAT) {
                ss << ".jpg";
            }
            else {
                cc::Logger::getInstance()->warning("unknown file type");
            }
            outfile = ss.str();
            
            cc::Logger::getInstance()->status("downloading "+outfile);
        }
        
        EdsStreamRef outStream;
        EDSDK_CHECK( EdsCreateFileStream(outfile.c_str(), kEdsFileCreateDisposition_CreateAlways, kEdsAccess_ReadWrite, &outStream) )
        EdsDownload(directoryItem, directoryItemInfo.size, outStream);
        EdsDownloadComplete(directoryItem);
        
        cc::Logger::getInstance()->status("releasing data stream");
        EDSDK_CHECK( EdsRelease(outStream) )
        
        //  Delete file after download
        if(deleteAfterDownload) {
            cc::Logger::getInstance()->status("deleting file from device");
            EDSDK_CHECK( EdsDeleteDirectoryItem(directoryItem) )
        }
        
        cc::Logger::getInstance()->status("downloaded "+outfile);
        
        outfile = "";
        downloading = false;
        
        EDSDK_CHECK( EdsRelease(object) )
        return EDS_ERR_OK;
    }

    
    // ----------------------------------------------------------------------
    bool Session::isRecording() {
        // Get the recording state.
        EdsUInt32 recordStart;
        EDSDK_CHECK( EdsGetPropertyData(camera, kEdsPropID_Record, 0, sizeof(recordStart), &recordStart) )
        return (recordStart==4);
    }

    // ----------------------------------------------------------------------
    void Session::process() {
        
        if(!sessionOpen) {
            open();
        }
        
        EDSDK_CHECK( EdsGetEvent() ) // I don't think this dos anything.
        
        if(command_queue.size()) {

            // Get the command from the queue
            command_queue_mutex.lock();
            command cmd = command_queue.back();
            command_queue.pop_back();
            command_queue_mutex.unlock();

            
            if(cmd[0].compare("record")==0) {
                if(isRecording()) {
                    Logger::getInstance()->warning("already recording");
                } else {
                    Logger::getInstance()->status("start recording");
                    EdsUInt32 record_start = 4; // Begin movie shooting
                    EDSDK_CHECK( EdsSetPropertyData(camera, kEdsPropID_Record, 0, sizeof(record_start), &record_start) )
                }
            }
            
            
            else if(cmd[0].compare("stop")==0) {
                if(!isRecording()) {
                    Logger::getInstance()->warning("not recording");
                } else {
                    
                    if(cmd.size() > 1) {
                        std::stringstream ss;
                        ss << defaultDir << "/" << cmd[1];
                        outfile = ss.str();
                        
                        
                        if(!overwrite && fileExists(outfile)) {
                            Logger::getInstance()->warning(outfile + " already exists. using defualt name instead");
                            outfile = "";
                        }
                    }
                    
                    Logger::getInstance()->status("stopping");
                    EdsUInt32 record_stop = 0; // End movie shooting
                    EDSDK_CHECK( EdsSetPropertyData(camera, kEdsPropID_Record, 0, sizeof(record_stop), &record_stop) )
                }
            }
            
            else if(cmd[0].compare("picture")==0) {
                // print_status("not yet implemented");
                if(isRecording()) {
                    Logger::getInstance()->warning("can't take a picture while recording");
                } else {
                    outfile = "";
                    if(cmd.size() > 1) {
                        outfile = cmd[1];
                    }
                    EDSDK_CHECK( EdsSendCommand(camera, kEdsCameraCommand_TakePicture, 0) )
                }
            }
            
            else if(cmd[0].compare("cancel")==0) {
                if(!isRecording()) {
                    Logger::getInstance()->warning("not recording");
                } else {
                    Logger::getInstance()->status("canceling");
                    canceled = true;
                    EdsUInt32 record_stop = 0; // End movie shooting
                    EDSDK_CHECK( EdsSetPropertyData(camera, kEdsPropID_Record, 0, sizeof(record_stop), &record_stop) )
                }
            }
            
            else if(cmd[0].compare("state")==0) {
                if(isRecording()) {
                    Logger::getInstance()->status("state recording");
                } else if(sessionOpen) {
                    Logger::getInstance()->status("state open");
                } else {
                    Logger::getInstance()->status("state closed");
                }

            }
            
            else {
                Logger::getInstance()->warning("unknown command: "+cmd[0]);
            }
        }
        
        
        time_point now = high_resolution_clock::now();
        //auto elapsed = now - start;
        //long secs = std::chrono::duration_cast<std::chrono::seconds>(elapsed).count();
        
        if( now > next_keepalive) {
            Logger::getInstance()->status("sending keep alive");
            EdsSendStatusCommand(camera, kEdsCameraCommand_ExtendShutDownTimer, 0);
            next_keepalive = now + std::chrono::seconds(60);
        }
    }

    
    // ----------------------------------------------------------------------
    void Session::updateCameraList() {
        EDSDK_CHECK( EdsGetCameraList(&cameraList) );
        EDSDK_CHECK( EdsGetChildCount(cameraList, &cameraCount) );
    }
    
    
    // ----------------------------------------------------------------------
    void Session::open() {
        if(sessionOpen) {
            cc::Logger::getInstance()->warning("session already open");
            return;
        }
        
        updateCameraList();
        
        if(cameraCount==0)
            throw std::runtime_error("no cameras connected.");
        
        if(cameraIndex < 0)
            throw std::runtime_error("invalid camera ID");
        
        if(cameraIndex >= cameraCount)
            throw std::runtime_error("invalid camera ID");
 
        
        std::stringstream msg;
        msg << "fetching camera " << cameraIndex;
        cc::Logger::getInstance()->status(msg.str());
        
        EDSDK_CHECK( EdsGetChildAtIndex(cameraList, cameraIndex, &camera) )
        
        EDSDK_CHECK( EdsSetObjectEventHandler(camera, kEdsObjectEvent_All, [](EdsObjectEvent event, EdsBaseRef object, EdsVoid* context) -> EdsError EDSCALLBACK {
            return reinterpret_cast<Session*>(context)->handleEvent(event, object);
        }, this) )
        EDSDK_CHECK( EdsSetPropertyEventHandler(camera, kEdsPropertyEvent_All, [](EdsPropertyEvent event, EdsPropertyID propertyId, EdsUInt32 param, EdsVoid* context) -> EdsError EDSCALLBACK {
            return reinterpret_cast<Session*>(context)->handleProperty(event, propertyId, param);
        }, this) )
        EDSDK_CHECK( EdsSetCameraStateEventHandler(camera, kEdsStateEvent_All, [](EdsStateEvent event, EdsUInt32 param, EdsVoid* context) -> EdsError EDSCALLBACK {
            return reinterpret_cast<Session*>(context)->handleState(event, param);
        }, this) )
        
        
        cc::Logger::getInstance()->status("opening session");
        EDSDK_CHECK( EdsOpenSession(camera) )
        sessionOpen = true;
        cc::Logger::getInstance()->status("opened session with "+getSerial());
        
        
        if(saveToHost) {
            cc::Logger::getInstance()->status("kEdsSaveTo_Host");
            EdsUInt32 saveTo = kEdsSaveTo_Host;
            EDSDK_CHECK( EdsSetPropertyData(camera, kEdsPropID_SaveTo, 0, sizeof(saveTo) , &saveTo) )
            
            EdsCapacity capacity;// = {0x7FFFFFFF, 0x1000, 1};
            capacity.reset = 1;
            capacity.bytesPerSector = 512*8;
            capacity.numberOfFreeClusters = 36864*9999;
            EDSDK_CHECK( EdsSetCapacity(camera, capacity) )
        } else {
            cc::Logger::getInstance()->status("kEdsSaveTo_Camera");
            EdsUInt32 saveTo = kEdsSaveTo_Camera;
            EDSDK_CHECK( EdsSetPropertyData(camera, kEdsPropID_SaveTo, 0, sizeof(saveTo), &saveTo) )
        }
    }


    // ----------------------------------------------------------------------
    std::string Session::getSerial() {
        EdsDataType dataType;
        EdsUInt32 dataSize;
        EdsGetPropertySize(camera, kEdsPropID_BodyIDEx, 0 , &dataType, &dataSize);
        char buf[dataSize];
        EDSDK_CHECK( EdsGetPropertyData(camera, kEdsPropID_BodyIDEx, 0, dataSize, &buf) )
        return std::string(buf);
    }

    
    
    
    
    
    #pragma mark EDSCALLBACK
    
    // ----------------------------------------------------------------------
    EdsError EDSCALLBACK Session::handleEvent(EdsObjectEvent event, EdsBaseRef object) {
        Logger::getInstance()->status(Eds::getObjectEventString(event));

        if(!object)
            return EDS_ERR_OK;
        if(event == kEdsObjectEvent_DirItemCreated) {
            if(canceled) {
                EDSDK_CHECK( EdsDeleteDirectoryItem(object) )
                canceled = false;
            } else {
                return download(object);
            }
        } else if(event == kEdsObjectEvent_DirItemRemoved) {
            cc::Logger::getInstance()->status("item removed");
        } else {
            EDSDK_CHECK( EdsRelease(object) )
        }
        
        return EDS_ERR_OK;
    }

    
    // ----------------------------------------------------------------------
    EdsError EDSCALLBACK Session::handleProperty(EdsPropertyEvent event, EdsPropertyID propertyId, EdsUInt32 param){
        std::stringstream ss;
        ss << Eds::getPropertyEventString(event) << ": " << Eds::getPropertyIDString(propertyId) << " / " << param;
        Logger::getInstance()->verbose(ss.str());
        
        return EDS_ERR_OK;
    }

    
    // ----------------------------------------------------------------------
    EdsError EDSCALLBACK Session::handleState(EdsStateEvent event, EdsUInt32 param){
    
        std::stringstream ss;
        ss << Eds::getPropertyEventString(event) << ": " << param;
        Logger::getInstance()->status(ss.str());
        
        if(event == kEdsStateEvent_ShutDownTimerUpdate) {
            cc::Logger::getInstance()->status("shutdown timer extended.");
        }
        else if(event == kEdsStateEvent_WillSoonShutDown) {
            cc::Logger::getInstance()->status("sending keep alive");
            EdsSendStatusCommand(camera, kEdsCameraCommand_ExtendShutDownTimer, 0);
        }
        else if(event == kEdsStateEvent_Shutdown) {
            cc::Logger::getInstance()->status("kEdsStateEvent_Shutdown received. camera disconnected");
            EDSDK_CHECK( EdsCloseSession(camera) )
            sessionOpen = false;
            downloading = false;
            exit(0);
        }
        else {
            cc::Logger::getInstance()->warning("unknown state");
        }
        
        return EDS_ERR_OK;
    }
}
