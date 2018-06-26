//
//  main.cpp
//  canon-video-capture
//
//  Created by Jeffrey Crouse on 8/18/17.
//  Copyright © 2017 See-through Lab. All rights reserved.
//




#include <thread>
#include "cxxopts.hpp"

#include "Logger.hpp"
#include "Session.hpp"

bool sigint = false;




//
//  TODO:
//   - Implement max record time?
//



int main(int argc, char * argv[]) {
    
    
    cc::Logger* log = cc::Logger::getInstance();
    cc::Session* session;
    try {
        session = cc::Session::getInstance();
    } catch(std::runtime_error e) {
        log->error(e.what());
        exit(1);
    }
    
    
    std::cout << "canon-camera-capture" << std::endl << std::endl;
    

    
    //
    //  Parse command line arguments
    //
    try {
        
        cxxopts::Options options(argv[0], "Command line program to capture video from a tethered Canon camera using EDSDK");
        options.add_options()
            ("d,debug", "Enable debugging", cxxopts::value<bool>())
            ("v,verbose", "Enable verbose output", cxxopts::value<bool>())
            ("i,id", "Device ID", cxxopts::value<EdsInt32>()->default_value("0")->implicit_value("0"))
            ("s,save-to-host", "Save to Host", cxxopts::value<bool>())
            ("o,overwrite", "Overwrite existing files", cxxopts::value<bool>())
            ("l,list-devices", "List Devices", cxxopts::value<bool>())
            ("x,delete-after-download", "Delete files after download", cxxopts::value<bool>())
            ("r,default-dir", "Default directory to save to if no path is given", cxxopts::value<std::string>())
            ("m,max-duration", "Maxium duration for video recording (in milliseconds)", cxxopts::value<int>()->default_value("-1")->implicit_value("-1"))
            ("help", "Print help")
            ;
        
        options.parse(argc, argv);
 
        if(options.count("help")) {
            std::cout << options.help() << std::endl;
            exit(0);
        }
        

        if(options["list-devices"].as<bool>()) {
            log->status("listing devices");
            try {
                std::cout << session->getDevicesAsJSON() << std::endl;
                delete session;
            } catch(std::runtime_error e) {
                 log->error(e.what());
            }
            exit(0);
        }
   
        if(options.count("debug")) {
            log->level = LOG_STATUS;
        }
        
        if(options.count("verbose")) {
            log->level = LOG_VERBOSE;
        }
        
        
        session->cameraIndex = options["id"].as<EdsInt32>();
        session->maxDuration = options["max-duration"].as<int>();
        session->deleteAfterDownload = options["delete-after-download"].as<bool>();
        session->defaultDir = options["default-dir"].as<std::string>();
        session->saveToHost = options["save-to-host"].as<bool>();
        session->overwrite = options["overwrite"].as<bool>();
        
        
        std::cout  << "id: " << session->cameraIndex << std::endl;
        std::cout  << "delete-after-download: " << (session->deleteAfterDownload ? "yes" : "no") << std::endl;
        std::cout  << "default-dir: " << session->defaultDir << std::endl;
        std::cout  << "save-to-host: " << (session->saveToHost?"yes":"no") << std::endl;
        std::cout  << "max-duration: " << session->maxDuration << std::endl;
        std::cout  << "overwrite: " << (session->overwrite ? "yes" : "no") << std::endl;
        
        
    } catch (const cxxopts::OptionException& e) {
        log->error(e.what());
        exit(1);
    }
    
    log->status("opening");

    

    
    
    
    //
    //  Input thread
    //
    std::thread input([&log, &session](){
        
        while(!sigint) {
//            if (isatty(STDIN_FILENO)){
//                std::cout << "> ";
//            }
            
            std::string input;
            std::getline(std::cin, input);
            
            if(session->downloading) {
                log->warning("can't execute commands while downloading");
            }
            
            // Clear out any quotes. It fucks shit up.
            input.erase( remove( input.begin(), input.end(), '\"' ), input.end() );
            
            
            // Split the input string into words
            cc::command cmd;
            std::istringstream iss(input);
            for(std::string s; iss >> s;) cmd.push_back(s);
            
            
            //print_status("\""+command+"\"");
            
            if (cmd[0].compare("exit") == 0) {
                log->status("exit");
                sigint = true;
            } else {
               session->addCommand(cmd);
            }
        }
    });
    
    
    //
    //  Main camera loop
    //
    while (!sigint) {
        CFRunLoopRunInMode( kCFRunLoopDefaultMode, 0, false); // https://stackoverflow.com/questions/23472376/canon-edsdk-handler-isnt-called-on-mac
        
        try {
            session->process();
        } catch(std::runtime_error e) {
            log->error(e.what());
            exit(1);
        }
        
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }


    //
    // Set the signal handler so we can tell when to shut down gracefully
    //
    signal(SIGINT, [](int signum) {
        std::cout << "Interrupt signal (" << signum << ") received." << std::endl;
        sigint = true;
    });
    
    
    
    //
    //  Terminate SDK
    //
    log->status("waiting for input thread");
    input.join();
    
    
    try {
        delete session;
    } catch(std::runtime_error e) {
         log->error(e.what());
    }
    
    log->status("exiting");
    return 0;
}
