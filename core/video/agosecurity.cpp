#include <stdio.h>
#include <unistd.h>
#include <time.h>
#include <syslog.h>
#include <cstdlib>
#include <iostream>
#include <sstream>
#include <algorithm>
#include <boost/date_time/posix_time/posix_time_types.hpp>
#include <boost/date_time/local_time/local_time.hpp>
#include <opencv2/opencv.hpp>
#include <queue>
#include <boost/algorithm/string/predicate.hpp>
#include <boost/algorithm/string/split.hpp>
#include <boost/algorithm/string.hpp>
#include <sys/wait.h>

#include "agoapp.h"
#include "frameprovider.h"

#ifndef SECURITYMAPFILE
#define SECURITYMAPFILE "maps/securitymap.json"
#endif

#ifndef RECORDINGSDIR
#define RECORDINGSDIR "recordings/"
#endif

using namespace qpid::messaging;
using namespace qpid::types;
using namespace agocontrol;
using namespace std;
using namespace cv;
namespace pt = boost::posix_time;
namespace fs = boost::filesystem;

enum TriggerStatus {
    Ok,
    OkInactiveZone,
    KoConfigInfoMissing,
    KoAlarmAlreadyRunning,
    KoInvalidConfig,
    KoAlarmFailed
};

enum ItemType {
    Device,
    Alarm
};

typedef struct TCurrentAlarm {
    std::string housemode;
    std::string zone;
} CurrentAlarm;

typedef struct TBox {
    int minX;
    int maxX;
    int minY;
    int maxY;
} Box;

class AgoSecurity: public AgoApp {
private:
    std::string agocontroller;
    boost::thread securityThread;
    bool isSecurityThreadRunning;
    bool wantSecurityThreadRunning;
    qpid::types::Variant::Map securitymap;
    qpid::types::Variant::Map inventory;
    std::string alertControllerUuid;
    bool isAlarmActivated;
    CurrentAlarm currentAlarm;
    qpid::types::Variant::Map alertGateways;
    pthread_mutex_t alertGatewaysMutex;
    pthread_mutex_t contactsMutex;
    pthread_mutex_t securitymapMutex;
    std::string email;
    std::string phone;
    bool stopProcess;
    void setupApp();
    void cleanupApp();

    //security
    bool checkPin(std::string _pin);
    bool setPin(std::string _pin);
    void enableAlarm(std::string zone, std::string housemode, int16_t delay);
    void disableAlarm(std::string zone, std::string housemode);
    void triggerAlarms(std::string zone, std::string housemode);
    qpid::types::Variant::Map commandHandler(qpid::types::Variant::Map content);
    void eventHandler(std::string subject, qpid::types::Variant::Map content);
    TriggerStatus triggerZone(std::string zone, std::string housemode);
    void getHousemodeItems(std::string zone, std::string housemode, ItemType type, qpid::types::Variant::List* items);
    bool changeHousemode(std::string housemode);
    void refreshAlertGateways();
    void refreshDefaultContact();
    void sendAlarm(std::string zone, std::string uuid, std::string message, qpid::types::Variant::Map* content);

    //video
    void getRecordings(std::string type, qpid::types::Variant::List& list);
    std::string getDateTimeString(bool date, bool time, bool withSeparator=true, std::string fieldSeparator="_");
    map<string, AgoFrameProvider*> frameProviders;
    AgoFrameProvider* getFrameProvider(string uri);
    pthread_mutex_t frameProvidersMutex;

    //timelapse
    bool stopTimelapses;
    std::map<std::string, boost::thread*> timelapseThreads;
    void fillTimelapse(qpid::types::Variant::Map* timelapse, qpid::types::Variant::Map* content);
    void timelapseFunction(string internalid, qpid::types::Variant::Map timelapse);
    void restartTimelapses();
    void launchTimelapses();
    void launchTimelapse(string internalid, qpid::types::Variant::Map& timelapse);
    void stopTimelapse(string internalid);

    //motion
    std::map<std::string, boost::thread*> motionThreads;
    void fillMotion(qpid::types::Variant::Map* timelapse, qpid::types::Variant::Map* content);
    void motionFunction(string internalid, qpid::types::Variant::Map timelapse);
    void launchMotions();
    void launchMotion(string internalid, qpid::types::Variant::Map& motion);
    void stopMotion(string internalid);

public:

    AGOAPP_CONSTRUCTOR_HEAD(AgoSecurity)
        , isSecurityThreadRunning(false)
        , wantSecurityThreadRunning(false)
        , isAlarmActivated(false)
        , email("")
        , phone("")
        , stopProcess(false)
        , stopTimelapses(false) {}
};

/**
 * Check pin code
 */
bool AgoSecurity::checkPin(std::string _pin)
{
    stringstream pins(getConfigOption("pin", "0815"));
    string pin;
    while (getline(pins, pin, ','))
    {
        if (_pin == pin) return true;
    }
    return false;
}

/**
 * Set pin codes
 */
bool AgoSecurity::setPin(std::string pin)
{
    return setConfigSectionOption("security", "pin", pin.c_str());
}

/**
 * Return list of alarms or devices for specified housemode/zone
 */
void AgoSecurity::getHousemodeItems(std::string zone, std::string housemode, ItemType type, qpid::types::Variant::List* items)
{
    if( items!=NULL )
    {
        if( !securitymap["config"].isVoid() )
        {
            qpid::types::Variant::Map config = securitymap["config"].asMap();
            for( qpid::types::Variant::Map::iterator it1=config.begin(); it1!=config.end(); it1++ )
            {
                //check housemode
                if( it1->first==housemode )
                {
                    //specified housemode found
                    qpid::types::Variant::List zones = it1->second.asList();
                    for( qpid::types::Variant::List::iterator it2=zones.begin(); it2!=zones.end(); it2++ )
                    {
                        //check zone
                        qpid::types::Variant::Map zoneMap = it2->asMap();
                        std::string zoneName = zoneMap["zone"].asString();
                        if( zoneName==zone )
                        {
                            //specified zone found, fill output list
                            if( type==Alarm )
                            {
                                if( !zoneMap["alarms"].isVoid() )
                                {
                                    *items = zoneMap["alarms"].asList();
                                }
                                else
                                {
                                    AGO_DEBUG() << "getHousemodeItems: invalid config, alarm list is missing!";
                                    return;
                                }
                            }
                            else
                            {
                                if( !zoneMap["devices"].isVoid() )
                                {
                                    *items = zoneMap["devices"].asList();
                                }
                                else
                                {
                                    AGO_DEBUG() << "getHousemodeItems: invalid config, device list is missing!";
                                    return;
                                }
                            }
                        }
                    }
                }
            }
        }
        else
        {
            AGO_DEBUG() << "getHousemodeItems: Invalid config file, config item is missing!";
        }
    }
    else
    {
        AGO_DEBUG() << "getHousemodeItems: Please give valid items object";
    }
}

/**
 * Set housemode
 */
bool AgoSecurity::changeHousemode(std::string housemode)
{
    //update security map
    AGO_INFO() << "Setting housemode: " << housemode;
    pthread_mutex_lock(&securitymapMutex);
    securitymap["housemode"] = housemode;
    agoConnection->setGlobalVariable("housemode", housemode);

    //send event that housemode changed
    Variant::Map eventcontent;
    eventcontent["housemode"]= housemode;
    agoConnection->emitEvent("securitycontroller", "event.security.housemodechanged", eventcontent);
    pthread_mutex_unlock(&securitymapMutex);

    //finally save changes to config file
    return variantMapToJSONFile(securitymap, getConfigPath(SECURITYMAPFILE));
}

/**
 * Enable alarm (threaded)
 * Handle countdown if necessary, send intruderalert event and send zone/housemode associated alarms
 */
void AgoSecurity::enableAlarm(std::string zone, std::string housemode, int16_t delay)
{
    //init
    Variant::Map content;
    Variant::Map countdownstartedeventcontent;
    countdownstartedeventcontent["delay"] = delay;
    countdownstartedeventcontent["zone"] = zone;

    //send countdown started event
    agoConnection->emitEvent("securitycontroller", "event.security.countdown.started", countdownstartedeventcontent);

    //run delay
    try
    {
        content["zone"] = zone;
        AGO_INFO() << "Alarm triggered: zone=" << zone << " housemode=" << housemode << " delay=" << delay;
        while (wantSecurityThreadRunning && delay-- > 0)
        {
            Variant::Map countdowneventcontent;
            countdowneventcontent["delay"] = delay;
            countdowneventcontent["zone"] = zone;
            AGO_TRACE() << "enableAlarm: countdown=" << delay;
            agoConnection->emitEvent("securitycontroller", "event.security.countdown", countdowneventcontent);
            boost::this_thread::sleep(pt::seconds(1));
        }

        //countdown expired
        AGO_INFO() << "Countdown expired for zone=" << zone << " housemode=" << housemode << " , sending 'intruder alert' event";
        //send event
        agoConnection->emitEvent("securitycontroller", "event.security.intruderalert", content);
        //and send alarms
        triggerAlarms(zone, housemode);
    }
    catch(boost::thread_interrupted &e)
    {
        //alarm has been cancelled by user
        AGO_DEBUG() << "Alarm thread cancelled";
        agoConnection->emitEvent("securitycontroller", "event.security.alarmcancelled", content);

        //finally change housemode to default one
        //this is implemented to toggle automatically to another housemode that disable alarm (but it's not mandatory)
        if( !securitymap["defaultHousemode"].isVoid() )
        {
            if( !changeHousemode(securitymap["defaultHousemode"].asString()) )
            {
                AGO_ERROR() << "Unable to write config file saving default housemode";
            }
        }
        else
        {
            AGO_DEBUG() << "enableAlarm: no default housemode, so current housemode is not changed";
        }
    }

    isSecurityThreadRunning = false;
    wantSecurityThreadRunning = false;
    AGO_DEBUG() << "Alarm thread exited";
}

/**
 * Disable current running alarm and stop enabled devices
 */
void AgoSecurity::disableAlarm(std::string zone, std::string housemode)
{
    AGO_INFO() << "Disabling alarm";
    Variant::Map content;
    qpid::types::Variant::List alarms;

    //first of all update flags
    isAlarmActivated = false;

    //and send events
    agoConnection->emitEvent("securitycontroller", "event.security.alarmstopped", content);

    //change housemode to default one
    //this is implemented to toggle automatically to another housemode that disable alarm (but it's not mandatory)
    if( !securitymap["defaultHousemode"].isVoid() )
    {
        if( !changeHousemode(securitymap["defaultHousemode"].asString()) )
        {
            AGO_ERROR() << "Unable to write config file saving default housemode";
        }
    }
    else
    {
        AGO_DEBUG() << "disableAlarm: no default housemode, so current housemode is not changed";
    }

    //then stop alarms
    getHousemodeItems(zone, housemode, Alarm, &alarms);
    for( qpid::types::Variant::List::iterator it=alarms.begin(); it!=alarms.end(); it++ )
    {
        qpid::types::Variant::Map content;
        std::string uuid = *it;

        //get message to send
        std::string message = "";
        if( !securitymap["disarmedMessage"].isVoid() )
        {
            message = securitymap["disarmedMessage"].asString();
        }
        if( message.length()==0 )
        {
            message = "Alarm disarmed";
        }

        //send event
        sendAlarm(zone, uuid, message, &content);
        AGO_DEBUG() << "cancelAlarm: disable alarm uuid='" << uuid << "' " << content;
    }
}

/**
 * Trigger alarms
 */
void AgoSecurity::triggerAlarms(std::string zone, std::string housemode)
{
    //get alarms
    qpid::types::Variant::List alarms;
    getHousemodeItems(zone, housemode, Alarm, &alarms);

    //send event for each alarm
    for( qpid::types::Variant::List::iterator it=alarms.begin(); it!=alarms.end(); it++ )
    {
        qpid::types::Variant::Map content;
        std::string uuid = *it;

        //get message to send
        std::string message = "";
        if( !securitymap["armedMessage"].isVoid() )
        {
            message = securitymap["armedMessage"].asString();
        }
        if( message.length()==0 )
        {
            message = "Alarm armed";
        }

        //send event
        sendAlarm(zone, uuid, message, &content);
        AGO_DEBUG() << "triggerAlarms: trigger device uuid='" << uuid << "' " << content;
    }
}

/**
 * Trigger zone enabling alarm
 */
TriggerStatus AgoSecurity::triggerZone(std::string zone, std::string housemode)
{
    if( !securitymap["config"].isVoid() )
    {
        qpid::types::Variant::Map config = securitymap["config"].asMap();
        AGO_TRACE() << " -> config=" << config;
        for( qpid::types::Variant::Map::iterator it1=config.begin(); it1!=config.end(); it1++ )
        {
            //check housemode
            AGO_TRACE() << " -> check housemode: " << it1->first << "==" << housemode;
            if( it1->first==housemode )
            {
                //specified housemode found
                qpid::types::Variant::List zones = it1->second.asList();
                AGO_TRACE() << " -> zones: " << zones;
                for( qpid::types::Variant::List::iterator it2=zones.begin(); it2!=zones.end(); it2++ )
                {
                    //check zone
                    qpid::types::Variant::Map zoneMap = it2->asMap();
                    std::string zoneName = zoneMap["zone"].asString();
                    int16_t delay = zoneMap["delay"].asInt16();
                    //check delay (if <0 it means inactive) and if it's current zone
                    AGO_TRACE() << " -> check zone: " << zoneName << "==" << zone << " delay=" << delay;
                    if( zoneName==zone )
                    {
                        //specified zone found
                        
                        //check delay
                        if( delay<0 )
                        {
                            //this zone is inactive
                            return OkInactiveZone;
                        }
                        
                        //check if there is already an alarm thread running
                        if( isSecurityThreadRunning==false )
                        {
                            //no alarm thread is running yet
                            try
                            {
                                //fill current alarm
                                currentAlarm.housemode = housemode;
                                currentAlarm.zone = zone;

                                // XXX: Does not handle multiple zone alarms.
                                wantSecurityThreadRunning = true;
                                isAlarmActivated = true;
                                securityThread = boost::thread(boost::bind(&AgoSecurity::enableAlarm, this, zone, housemode, delay));
                                isSecurityThreadRunning = true;
                                return Ok;
                            }
                            catch(std::exception& e)
                            {
                                AGO_FATAL() << "Failed to start alarm thread! " << e.what();
                                return KoAlarmFailed;
                            }
                        }
                        else
                        {
                            //alarm thread already running
                            AGO_DEBUG() << "Alarm thread is already running";
                            return KoAlarmAlreadyRunning;
                        }

                        //stop statement here for this housemode
                        break;
                    }
                }
            }
        }

        //no housemode/zone found
        AGO_ERROR() << "Specified housemode/zone '" << housemode << "/" << zone << "' doesn't exist";
        return KoConfigInfoMissing;
    }
    else
    {
        //invalid config
        return KoInvalidConfig;
    }
}

/**
 * Send an alarm
 */
void AgoSecurity::sendAlarm(std::string zone, std::string uuid, std::string message, qpid::types::Variant::Map* content)
{
    bool found = false;
    bool send = true;
    AGO_TRACE() << "sendAlarm() BEGIN";

    pthread_mutex_lock(&alertGatewaysMutex);
    for( qpid::types::Variant::Map::iterator it=alertGateways.begin(); it!=alertGateways.end(); it++ )
    {
        if( it->first==uuid )
        {
            //gateway found
            found = true;
            std::string gatewayType = it->second.asString();
            if( gatewayType=="smsgateway" )
            {
                pthread_mutex_lock(&contactsMutex);
                if( phone.size()>0 )
                {
                    (*content)["command"] = "sendsms";
                    (*content)["uuid"] = uuid;
                    (*content)["to"] = phone;
                    (*content)["text"] = message + "[" + zone + "]";
                }
                else
                {
                    AGO_WARNING() << "Trying to send alert to undefined phone number. You must configure default one in system config";
                    send = false;
                }
                pthread_mutex_unlock(&contactsMutex);
            }
            else if( gatewayType=="smtpgateway" )
            {
                pthread_mutex_lock(&contactsMutex);
                if( phone.size()>0 )
                {
                    (*content)["command"] = "sendmail";
                    (*content)["uuid"] = uuid;
                    (*content)["to"] = email;
                    (*content)["subject"] = "Agocontrol security";
                    (*content)["body"] = message + "[" + zone + "]";
                }
                else
                {
                    AGO_WARNING() << "Trying to send alert to undefined email address. You must configure default one in system config";
                    send = false;
                }
                pthread_mutex_unlock(&contactsMutex);
            }
            else if( gatewayType=="twittergateway" )
            {
                (*content)["command"] = "sendtweet";
                (*content)["uuid"] = uuid;
                (*content)["tweet"] = message + "[" + zone + "]";
            }
            else if( gatewayType=="pushgateway" )
            {
                (*content)["command"] = "sendpush";
                (*content)["uuid"] = uuid;
                (*content)["message"] = message + "[" + zone + "]";
            }
        }
    }
    pthread_mutex_unlock(&alertGatewaysMutex);

    if( !found )
    {
        //switch type device
        (*content)["command"] = "on";
        (*content)["uuid"] = uuid;
    }

    if( send )
    {
        agoConnection->sendMessageReply("", *content);
    }
    AGO_TRACE() << "sendAlarm() END";
}

/**
 * Refresh default contact informations (mail + phone number)
 */
void AgoSecurity::refreshDefaultContact()
{
    pthread_mutex_lock(&contactsMutex);
    std::string oldEmail = email;
    std::string oldPhone = phone;
    email = getConfigOption("email", "", "system", "system");
    phone = getConfigOption("phone", "", "system", "system");
    AGO_TRACE() << "read email=" << email;
    AGO_TRACE() << "read phone=" << phone;
    if( oldEmail!=email )
    {
        AGO_DEBUG() << "Default email changed (now " << email << ")";
    }
    if( oldPhone!=phone )
    {
        AGO_DEBUG() << "Default phone number changed (now " << phone << ")";
    }
    pthread_mutex_unlock(&contactsMutex);
}

/**
 * Refresh alert gateways
 */
void AgoSecurity::refreshAlertGateways()
{
    //get alert controller uuid
    qpid::types::Variant::Map inventory = agoConnection->getInventory();
    qpid::types::Variant::List gatewayTypes;
    
    if( inventory.size()>0 )
    {
        //get usernotification category devicetypes
        if( !inventory["schema"].isVoid() )
        {
            qpid::types::Variant::Map schema = inventory["schema"].asMap();
            if( !schema["categories"].isVoid() )
            {
                qpid::types::Variant::Map categories = schema["categories"].asMap();
                if( !categories["usernotification"].isVoid() )
                {
                    qpid::types::Variant::Map usernotification = categories["usernotification"].asMap();
                    if( !usernotification["devicetypes"].isVoid() )
                    {
                        gatewayTypes = usernotification["devicetypes"].asList();
                    }
                }
            }
        }

        //get available alert gateway uuids
        if( !inventory["devices"].isVoid() )
        {
            pthread_mutex_lock(&alertGatewaysMutex);

            //clear list
            alertGateways.clear();

            //fill with fresh data
            qpid::types::Variant::Map devices = inventory["devices"].asMap();
            for( qpid::types::Variant::Map::iterator it1=devices.begin(); it1!=devices.end(); it1++ )
            {
                std::string uuid = it1->first;
                if( !it1->second.isVoid() )
                {
                    qpid::types::Variant::Map deviceInfos = it1->second.asMap();
                    for( qpid::types::Variant::List::iterator it2=gatewayTypes.begin(); it2!=gatewayTypes.end(); it2++ )
                    {
                        if( !deviceInfos["devicetype"].isVoid() && deviceInfos["devicetype"].asString()==(*it2) )
                        {
                            AGO_TRACE() << "Found alert " << (*it2) << " with uuid " << uuid << " [name=" << deviceInfos["name"].asString() << "]";
                            alertGateways[uuid] = (*it2);
                        }
                    }
                }
            }

            pthread_mutex_unlock(&alertGatewaysMutex);
        }
    }
    AGO_TRACE() << "Found alert gateways: " << alertGateways;
}

/**
 * Return frame provider. If provider doesn't exist for specified uri, new one is created
 * and returned
 */
AgoFrameProvider* AgoSecurity::getFrameProvider(string uri)
{
    AgoFrameProvider* out = NULL;

    pthread_mutex_lock(&frameProvidersMutex);

    //search existing frame provider
    map<string, AgoFrameProvider*>::iterator item = frameProviders.find(uri);
    if( item==frameProviders.end() )
    {
        AGO_DEBUG() << "Create new frame provider '" << uri << "'";
        //frame provider doesn't exist for specified uri, create new one
        AgoFrameProvider* provider = new AgoFrameProvider(uri);
        if( !provider->start() )
        {
            //unable to start frame provider, uri is valid?
            return NULL;
        }
        frameProviders[uri] = provider;
        out = provider;
    }
    else
    {
        AGO_DEBUG() << "Frame provider already exists for '" << uri << "'";
        out = item->second;
    }

    pthread_mutex_unlock(&frameProvidersMutex);

    return out;
}

/**
 * Timelapse function (threaded)
 */
void AgoSecurity::timelapseFunction(string internalid, qpid::types::Variant::Map timelapse)
{
    //using fork is a workaround of cv::VideoWriter memory leak
    char pipeBuffer[10];
    int pipefd[2];
    if( pipe(pipefd) )
    {
        AGO_ERROR() << "Timelapse '" << internalid << "': unable to create communication pipe";
        return;
    }
    if( fcntl(pipefd[0], F_SETFL, O_NONBLOCK)==-1 )
    {
        AGO_ERROR() << "Timelapse '" << internalid << "': unable to make non blockling communication pipe";
        return;
    }

    //init video reader (provider and consumer)
    string timelapseUri = timelapse["uri"].asString();
    AgoFrameProvider* provider = getFrameProvider(timelapseUri);
    if( provider==NULL )
    {
        //no frame provider
        AGO_ERROR() << "Timelapse '" << internalid << "': stopped because no provider available";
        return;
    }
    AgoFrameConsumer consumer;
    provider->subscribe(&consumer);

    //create child process
    pid_t pid = fork();
    if( pid==0 )
    {
        //child process
        close(pipefd[1]);

        AGO_DEBUG() << "Timelapse '" << internalid << "': started";

        //create timelapse device
        //TO MOVE agoConnection->addDevice(internalid.c_str(), "timelapse");
    
        //init video writer
        bool fileOk = false;
        int inc = 0;
        fs::path filepath;
        while( !fileOk )
        {
            std::string name = timelapse["name"].asString();
            stringstream filename;
            filename << RECORDINGSDIR;
            filename << "timelapse_";
            filename << internalid << "_";
            filename << getDateTimeString(true, false, false);
            if( inc>0 )
            {
                filename << "_" << inc;
            }
            filename << ".avi";
            filepath = ensureParentDirExists(getLocalStatePath(filename.str()));
            if( fs::exists(filepath) )
            {
                //file already exists
                inc++;
            }
            else
            {
                fileOk = true;
            }
        }
        AGO_DEBUG() << "Record into '" << filepath.c_str() << "'";
        string codec = timelapse["codec"].asString();
        int fourcc = CV_FOURCC('F', 'M', 'P', '4');
        if( codec.length()==4 )
        {
            fourcc = CV_FOURCC(codec[0], codec[1], codec[2], codec[3]);
        }
        int fps = 24;
        VideoWriter recorder(filepath.c_str(), fourcc, fps, provider->getResolution());
        if( !recorder.isOpened() )
        {
            //XXX emit error?
            AGO_ERROR() << "Timelapse '" << internalid << "': unable to open recorder";
            return;
        }
    
        //try
        //{
            int now = (int)(time(NULL));
            int last = 0;
            int nread = 0;
            Mat frame;
            bool continu = true;
            while( continu )
            {
                if( provider->isRunning() )
                {
                    //get frame in any case (to empty queue)
                    frame = consumer.popFrame(&boost::this_thread::sleep_for);
                    recorder << frame;
    
                    //TODO handle recording fps using timelapse["fps"] param.
                    //For now it records at 1 fps to minimize memory leak of cv::VideoWriter
                    if( now!=last )
                    {
                        //need to copy frame to alter it
                        Mat copiedFrame = frame.clone();
    
                        //add text
                        stringstream stream;
                        stream << getDateTimeString(true, true, true, " ");
                        stream << " - " << timelapse["name"].asString();
                        string text = stream.str();
    
                        try
                        {
                            putText(copiedFrame, text.c_str(), Point(20,20), FONT_HERSHEY_SIMPLEX, 0.5, Scalar(0,0,0), 4, CV_AA);
                            putText(copiedFrame, text.c_str(), Point(20,20), FONT_HERSHEY_SIMPLEX, 0.5, Scalar(255,255,255), 1, CV_AA);
                        }
                        catch(cv::Exception& e)
                        {
                            AGO_ERROR() << "Timelapse '" << internalid << "': opencv exception occured " << e.what();
                        }
    
                        //record frame
                        recorder << copiedFrame;
                        last = now;
                    }
                }
        
                //update current time
                now = (int)(time(NULL));
    
                //check if thread has been interrupted
                //boost::this_thread::interruption_point();
                
                //check if stop has been requested
                switch( nread=read(pipefd[0], pipeBuffer, 10) )
                {
                    case -1:
                        //make sure pipe is empty
                        if( errno==EAGAIN )
                        {
                            //nothing to do
                        }
                        else
                        {
                            //unable to read pipe content
                            AGO_ERROR() << "Timelapse '" << internalid << "': unable to read pipe content";
                            continu = false;
                        }
                        break;
                    case 0:
                        //pipe is closed
                        AGO_DEBUG() << "Timelapse '" << internalid << "': read pipe is closed";
                        continu = false;
                        break;
                    default:
                        //nothing to do
                        break;
                }
            }
        /*}
        catch(boost::thread_interrupted &e)
        {
            AGO_DEBUG() << "Timelapse '" << internalid << "': thread interrupted";
        }*/
    
        //close all
        AGO_DEBUG() << "Timelapse '" << internalid << "': close recorder";
        if( recorder.isOpened() )
        {
            recorder.release();
        }
    
        //remove timelapse device
        //AGO_DEBUG() << "Timelapse '" << internalid << "': remove device";
        //TO MOVE agoConnection->removeDevice(internalid.c_str());
    
        AGO_DEBUG() << "Timelapse '" << internalid << "': stopped";

        return;
    }
    else if( pid<0 )
    {
        //fork failed
        AGO_ERROR() << "Timelapse '" << internalid << "': unable to create fork";
        return;
    }
    else
    {
        //father process
        close(pipefd[0]);
        
        try
        {
            //while( !stopProcess && !stopTimelapses )
            while(true)
            {
            /*int stat_val;
            wait(&stat_val);
            if(WIFEXITED(stat_val))
            {
                AGO_INFO() << "son terminated";
            }
            else
            {
                AGO_ERROR() << "Son closed abnormally";
            }*/

                //sleep thread (and also catch interruption)
                boost::this_thread::sleep_for(boost::chrono::milliseconds(100));
            }
            AGO_DEBUG() << "Timelapse '" << internalid << "': end of timelapse";
            close(pipefd[1]);

            AGO_DEBUG() << "Timelapse '" << internalid << "': wait for end of child #1";
            wait(NULL);
            AGO_DEBUG() << "Timelapse '" << internalid << "': child ended #1";
        }
        catch(boost::thread_interrupted &e)
        {
            AGO_DEBUG() << "Timelapse '" << internalid << "': thread interrupted";

            //force child to stop process closing pipe
            close(pipefd[1]);

            AGO_DEBUG() << "Timelapse '" << internalid << "': wait for end of child #2";
            wait(NULL);
            AGO_DEBUG() << "Timelapse '" << internalid << "': child ended #2";
        }
    }

    //unsubscribe from provider
    AGO_DEBUG() << "Timelapse '" << internalid << "': unsubscribe from frame provider";
    provider->unsubscribe(&consumer);
}

/**
 * Fill timelapse map
 * @param timelapse: map to fill
 * @param content: map from request used to prefill output map
 */
void AgoSecurity::fillTimelapse(qpid::types::Variant::Map* timelapse, qpid::types::Variant::Map* content=NULL)
{
    if( content==NULL )
    {
        //fill with default values
        (*timelapse)["name"] = "noname";
        (*timelapse)["uri"] = "";
        (*timelapse)["fps"] = 1;
        (*timelapse)["codec"] = "FMP4";
        (*timelapse)["enabled"] = true;
    }
    else
    {
        //fill with specified content
        if( !(*content)["name"].isVoid() )
            (*timelapse)["name"] = (*content)["name"].asString();
        else
            (*timelapse)["name"] = "noname";

        if( !(*content)["uri"].isVoid() )
            (*timelapse)["uri"] = (*content)["uri"].asString();
        else
            (*timelapse)["uri"] = "";

        if( !(*content)["fps"].isVoid() )
            (*timelapse)["fps"] = (*content)["fps"].asInt32();
        else
            (*timelapse)["fps"] = 1;

        if( !(*content)["codec"].isVoid() )
            (*timelapse)["codec"] = (*content)["codec"].asString();
        else
            (*timelapse)["codec"] = "FMP4";

        if( !(*content)["enabled"].isVoid() )
            (*timelapse)["enabled"] = (*content)["enabled"].asBool();
        else
            (*timelapse)["enabled"] = true;
    }
}

/**
 * Restart timelapses
 */
void AgoSecurity::restartTimelapses()
{
    //stop current timelapses
    stopTimelapses = true;
    for( std::map<std::string, boost::thread*>::iterator it=timelapseThreads.begin(); it!=timelapseThreads.end(); it++ )
    {
        stopTimelapse(it->first);
    }

    //then restart them all
    stopTimelapses = false;
    launchTimelapses();
}

/**
 * Launch all timelapses
 */
void AgoSecurity::launchTimelapses()
{
    qpid::types::Variant::Map timelapses = securitymap["timelapses"].asMap();
    for( qpid::types::Variant::Map::iterator it=timelapses.begin(); it!=timelapses.end(); it++ )
    {
        string internalid = it->first;
        qpid::types::Variant::Map timelapse = it->second.asMap();
        launchTimelapse(internalid, timelapse);
    }
}

/**
 * Launch specified timelapse
 */
void AgoSecurity::launchTimelapse(string internalid, qpid::types::Variant::Map& timelapse)
{
    AGO_DEBUG() << "Launch timelapse '" << internalid << "'";
    if( timelapse["enabled"].asBool()==true )
    {
        boost::thread* thread = new boost::thread(boost::bind(&AgoSecurity::timelapseFunction, this, internalid, timelapse));
        timelapseThreads[internalid] = thread;
    }
    else
    {
        AGO_DEBUG() << " -> not launch because timelapse is disabled";
    }
}

/**
 * Stop timelapse thread
 */
void AgoSecurity::stopTimelapse(string internalid)
{
    //stop thread
    timelapseThreads[internalid]->interrupt();
    timelapseThreads[internalid]->join();

    //remove thread from list
    timelapseThreads.erase(internalid);
}

/**
 * Detect motion function
 * @return number of changes
 */
inline int detectMotion(const Mat& motion, Mat& result, Box& area, int maxDeviation, Scalar& color)
{
    //calculate the standard deviation
    Scalar mean, stddev;
    meanStdDev(motion, mean, stddev);
    //AGO_DEBUG() << "stddev[0]=" << stddev[0];

    //if not to much changes then the motion is real (neglect agressive snow, temporary sunlight)
    if( stddev[0]<maxDeviation )
    {
        int numberOfChanges = 0;
        int minX = motion.cols, maxX = 0;
        int minY = motion.rows, maxY = 0;

        // loop over image and detect changes
        for( int j=area.minY; j<area.maxY; j+=2 ) // height
        {
            for( int i=area.minX; i<area.maxX; i+=2 ) // width
            {
                //check if at pixel (j,i) intensity is equal to 255
                //this means that the pixel is different in the sequence
                //of images (prev_frame, current_frame, next_frame)
                if( static_cast<int>(motion.at<uchar>(j,i))==255 )
                {
                    numberOfChanges++;
                    if( minX>i ) minX = i;
                    if( maxX<i ) maxX = i;
                    if( minY>j ) minY = j;
                    if( maxY<j ) maxY = j;
                }
            }
        }

        if( numberOfChanges )
        {
            //check if not out of bounds
            if( minX-10>0) minX -= 10;
            if( minY-10>0) minY -= 10;
            if( maxX+10<result.cols-1 ) maxX += 10;
            if( maxY+10<result.rows-1 ) maxY += 10;

            // draw rectangle round the changed pixel
            Point x(minX, minY);
            Point y(maxX, maxY);
            Rect rect(x, y);
            rectangle(result, rect, color, 2);
        }

        return numberOfChanges;
    }

    return 0;
}

/**
 * Motion function (threaded)
 * Based on Cédric Verstraeten source code: https://github.com/cedricve/motion-detection/blob/master/motion_src/src/motion_detection.cpp
 */
void AgoSecurity::motionFunction(string internalid, qpid::types::Variant::Map motion)
{
    AGO_DEBUG() << "Motion '" << internalid << "': started";

    //create motion device
    string motionUri = motion["uri"].asString();
    agoConnection->addDevice(internalid.c_str(), "motionsensor");

    //init video reader (provider and consumer)
    AgoFrameProvider* provider = getFrameProvider(motionUri);
    if( provider==NULL )
    {
        //no frame provider
        AGO_ERROR() << "Motion '" << internalid << "': stopped because no provider available";
        return;
    }
    AgoFrameConsumer consumer;
    provider->subscribe(&consumer);
    Size resolution = provider->getResolution();
    int fps = provider->getFps();
    AGO_DEBUG() << "Motion '" << internalid << "': fps=" << fps;

    //init buffer
    unsigned int maxBufferSize = motion["bufferduration"].asInt32() * fps;
    std::queue<Mat> buffer;

    //get frames and convert to gray
    //AgoFrame* frame = NULL;
    Mat prevFrame, currentFrame, nextFrame, result, tempFrame;
    if( provider->isRunning() )
    {
        //frame = consumer.popFrame(&boost::this_thread::sleep_for);
        tempFrame = consumer.popFrame(&boost::this_thread::sleep_for);
        //prevFrame = temp.clone();
        cvtColor(tempFrame, prevFrame, CV_RGB2GRAY);
        //frame->done();

        //frame = consumer.popFrame(&boost::this_thread::sleep_for);
        //currentFrame = frame->frame.clone();
        tempFrame = consumer.popFrame(&boost::this_thread::sleep_for);
        //currentFrame = temp.clone();
        cvtColor(tempFrame, currentFrame, CV_RGB2GRAY);
        //frame->done();

        //frame = consumer.popFrame(&boost::this_thread::sleep_for);
        //nextFrame = frame->frame.clone();
        tempFrame = consumer.popFrame(&boost::this_thread::sleep_for);
        //nextFrame = temp.clone();
        cvtColor(tempFrame, nextFrame, CV_RGB2GRAY);
        //frame->done();
    }

    //other declarations
    std::string name = motion["name"].asString();
    fs::path recordPath;
    int fourcc = CV_FOURCC('F', 'M', 'P', '4');
    int now = (int)time(NULL);
    int startup = now;
    int onDuration = motion["onduration"].asInt32() - motion["bufferduration"].asInt32();
    int recordDuration = motion["recordduration"].asInt32();
    bool isRecording, isTriggered = false;
    int triggerStart = 0;
    Mat d1, d2, _motion;
    int numberOfChanges = 0;
    Scalar color(0,0,255);
    int thereIsMotion = motion["sensitivity"].asInt32();
    int maxDeviation = motion["deviation"].asInt32();
    AGO_TRACE() << "Motion '" << internalid << "': maxdeviation=" << maxDeviation << " sensitivity=" << thereIsMotion;
    Mat kernelErode = getStructuringElement(MORPH_RECT, Size(2,2));
    Box area = {0, currentFrame.cols, 0, currentFrame.rows};
    AGO_TRACE() << "Motion '" << internalid << "': area minx=" << area.minX << " maxx=" << area.maxX << " miny=" << area.minY << " maxy=" << area.maxY;
    VideoWriter recorder("", 0, 1, Size(10,10)); //dummy recorder, unable to compile with empty constructor :S

    //debug purpose: display frame in window
    //namedWindow("Display window", WINDOW_AUTOSIZE );

    try
    {
        while( !stopProcess )
        {
            if( provider->isRunning() )
            {
                //get new frame
                prevFrame = currentFrame;
                currentFrame = nextFrame;
                //frame = consumer.popFrame(&boost::this_thread::sleep_for);
                //nextFrame = frame->frame; //copy frame to alter it (add text)
                tempFrame = consumer.popFrame(&boost::this_thread::sleep_for);
                //nextFrame = temp.clone();
                //frame->done();
                result = tempFrame; //keep color copy
                cvtColor(tempFrame, nextFrame, CV_RGB2GRAY);

                //add text to frame (current time and motion name)
                stringstream stream;
                stream << getDateTimeString(true, true, true, " ");
                if( name.length()>0 )
                {
                    stream << " - " << name;
                }
                string text = stream.str();
                try
                {
                    putText(result, text.c_str(), Point(20,20), FONT_HERSHEY_SIMPLEX, 0.5, Scalar(0,0,0), 4, CV_AA);
                    putText(result, text.c_str(), Point(20,20), FONT_HERSHEY_SIMPLEX, 0.5, Scalar(255,255,255), 1, CV_AA);
                }
                catch(cv::Exception& e)
                {
                    AGO_ERROR() << "Motion '" << internalid << "': opencv exception #1 occured " << e.what();
                }

                //handle buffer
                if( !isRecording )
                {
                    while( buffer.size()>=maxBufferSize )
                    {
                        //remove old frames
                        buffer.pop();
                    }
                    buffer.push(result);
                }

                //calc differences between the images and do AND-operation
                //threshold image, low differences are ignored (ex. contrast change due to sunlight)
                try
                {
                    absdiff(prevFrame, nextFrame, d1);
                    absdiff(nextFrame, currentFrame, d2);
                    bitwise_and(d1, d2, _motion);
                    threshold(_motion, _motion, 35, 255, CV_THRESH_BINARY);
                    erode(_motion, _motion, kernelErode);
                }
                catch(cv::Exception& e)
                {
                    AGO_ERROR() << "Motion '" << internalid << "': opencv exception #2 occured " << e.what();
                }

                //debug purpose: display frame in window
                //imshow("Display window", _motion);
                
                //check if thread has been interrupted
                boost::this_thread::interruption_point();

                //update current time
                now = (int)time(NULL);

                //drop first 5 seconds for stabilization
                if( now<=(startup+5) )
                {
                    continue;
                }

                //detect motion
                numberOfChanges = 0;
                try
                {
                    numberOfChanges = detectMotion(_motion, result, area, maxDeviation, color);
                }
                catch(cv::Exception& e)
                {
                    AGO_ERROR() << "Motion '" << internalid << "': opencv exception #3 occured " << e.what();
                }

                if( !isTriggered )
                {
                    //analyze motion
                    if( numberOfChanges>=thereIsMotion )
                    {
                        //save picture and send pictureavailable event
                        stringstream filename;
                        filename << "/tmp/" << internalid << ".jpg";
                        std::string picture = filename.str();
                        try
                        {
                            imwrite(picture.c_str(), result);
                            qpid::types::Variant::Map content;
                            content["filename"] = picture;
                            agoConnection->emitEvent(internalid.c_str(), "event.device.pictureavailable", content);
                        }
                        catch(...)
                        {
                            AGO_ERROR() << "Motion '" << internalid << "': Unable to write motion picture '" << picture << "' to disk";
                        }
                
                        //prepare recorder
                        filename.str("");
                        filename << RECORDINGSDIR;
                        filename << "motion_";
                        filename << internalid << "_";
                        filename << getDateTimeString(true, true, false, "_");
                        filename << ".avi";
                        recordPath = ensureParentDirExists(getLocalStatePath(filename.str()));
                        AGO_DEBUG() << "Motion '" << internalid << "': record to " << recordPath.c_str();

                        try
                        {
                            recorder.open(recordPath.c_str(), fourcc, fps, resolution);
                            if( !recorder.isOpened() )
                            {
                                //XXX emit error?
                                AGO_ERROR() << "Motion '" << internalid << "': unable to open recorder";
                            }
                            triggerStart = (int)time(NULL);
                            AGO_DEBUG() << "Motion '" << internalid << "': enable motion trigger and start motion recording";
                            isRecording = true;
                            isTriggered = true;
    
                            //and empty buffer into recorder
                            while( buffer.size()>0 )
                            {
                                recorder << buffer.front();
                                buffer.pop();
                            }
                        }
                        catch(cv::Exception& e)
                        {
                            AGO_ERROR() << "Motion '" << internalid << "': opencv exception #4 occured " << e.what();
                        }

                        //emit security event (enable motion sensor)
                        agoConnection->emitEvent(internalid.c_str(), "event.device.statechanged", 255, "");
                    }
                }
                else
                {
                    //handle recording
                    if( isRecording && now>=(triggerStart+recordDuration) )
                    {
                        AGO_DEBUG() << "Motion '" << internalid << "': stop motion recording";

                        //stop recording
                        if( recorder.isOpened() )
                        {
                            recorder.release();
                        }
                        isRecording = false;

                        //emit video available event
                        qpid::types::Variant::Map content;
                        content["filename"] = recordPath.c_str();
                        agoConnection->emitEvent(internalid.c_str(), "event.device.videoavailable", content);
                    }
                    else
                    {
                        //save current frame
                        recorder << result;
                    }

                    //handle trigger
                    if( isTriggered && now>=(triggerStart+onDuration) )
                    {
                        AGO_DEBUG() << "Motion '" << internalid << "': disable motion trigger";
                        isTriggered = false;

                        //emit security event (disable motion sensor)
                        agoConnection->emitEvent(internalid.c_str(), "event.device.statechanged", 0, "");
                    }
                }
            }

            //debug purpose: display frame in window
            //waitKey(5);
            
            //check if thread has been interrupted
            boost::this_thread::interruption_point();
        }
    }
    catch(boost::thread_interrupted &e)
    {
        AGO_DEBUG() << "Motion '" << internalid << "': thread interrupted";
    }

    //close all
    provider->unsubscribe(&consumer);
    if( recorder.isOpened() )
    {
        recorder.release();
    }

    //remove motion device
    agoConnection->removeDevice(internalid.c_str());

    AGO_DEBUG() << "Motion '" << internalid << "': stopped";
}

/**
 * Fill motion map
 * @param motion: map to fill
 * @param content: content from request. This map is used to prefill output map
 */
void AgoSecurity::fillMotion(qpid::types::Variant::Map* motion, qpid::types::Variant::Map* content=NULL)
{
    if( content==NULL )
    {
        //fill with default parameters
        (*motion)["name"] = "noname";
        (*motion)["uri"] = "";
        (*motion)["deviation"] = 20;
        (*motion)["sensitivity"] = 10;
        (*motion)["bufferduration"] = 10;
        (*motion)["onduration"] = 300;
        (*motion)["recordduration"] = 30;
        (*motion)["enabled"] = true;
    }
    else
    {
        //fill with content
        if( !(*content)["name"].isVoid() )
            (*motion)["name"] = (*content)["name"].asString();
        else
            (*motion)["name"] = "noname";

        if( !(*content)["uri"].isVoid() )
            (*motion)["uri"] = (*content)["uri"].asString();
        else
            (*motion)["uri"] = "";

        if( !(*content)["deviation"].isVoid() )
            (*motion)["deviation"] = (*content)["deviation"].asInt32();
        else
            (*motion)["deviation"] = 20;

        if( !(*content)["sensitivity"].isVoid() )
            (*motion)["sensitivity"] = (*content)["sensitivity"].asInt32();
        else
            (*motion)["sensitivity"] = 10;

        if( !(*content)["bufferduration"].isVoid() )
            (*motion)["bufferduration"] = (*content)["bufferduration"].asInt32();
        else
            (*motion)["bufferduration"] = 1;

        if( !(*content)["onduration"].isVoid() )
            (*motion)["onduration"] = (*content)["onduration"].asInt32();
        else
            (*motion)["onduration"] = 300;

        if( !(*content)["recordduration"].isVoid() )
            (*motion)["recordduration"] = (*content)["recordduration"].asInt32();
        else
            (*motion)["recordduration"] = 30;

        if( !(*content)["enabled"].isVoid() )
            (*motion)["enabled"] = (*content)["enabled"].asBool();
        else
            (*motion)["enabled"] = true;
    }
}

/**
 * Launch all motions
 */
void AgoSecurity::launchMotions()
{
    qpid::types::Variant::Map motions = securitymap["motions"].asMap();
    for( qpid::types::Variant::Map::iterator it=motions.begin(); it!=motions.end(); it++ )
    {
        string internalid = it->first;
        qpid::types::Variant::Map motion = it->second.asMap();
        launchMotion(internalid, motion);
    }
}

/**
 * Launch specified motion
 */
void AgoSecurity::launchMotion(string internalid, qpid::types::Variant::Map& motion)
{
    AGO_DEBUG() << "Launch motion: " << internalid;
    if( motion["enabled"].asBool()==true )
    {
        boost::thread* thread = new boost::thread(boost::bind(&AgoSecurity::motionFunction, this, internalid, motion));
        motionThreads[internalid] = thread;
    }
    else
    {
        AGO_DEBUG() << " -> not launch because motion is disabled";
    }
}

/**
 * Stop motion thread
 */
void AgoSecurity::stopMotion(string internalid)
{
    //stop thread
    motionThreads[internalid]->interrupt();
    motionThreads[internalid]->join();

    //remove thread from list
    motionThreads.erase(internalid);
}

/**
 * Get recordings of specified type
 */
void AgoSecurity::getRecordings(std::string type, qpid::types::Variant::List& list)
{
    fs::path recordingsPath = getLocalStatePath(RECORDINGSDIR);
    if( fs::exists(recordingsPath) )
    {
        fs::recursive_directory_iterator it(recordingsPath);
        fs::recursive_directory_iterator endit;
        while( it!=endit )
        {
            if( fs::is_regular_file(*it) && it->path().extension().string()==".avi" && boost::algorithm::starts_with(it->path().filename().string(), type) )
            {
                qpid::types::Variant::Map props;
                props["filename"] = it->path().filename().string();
                props["path"] = it->path().string();
                props["size"] = fs::file_size(it->path());
                props["date"] = fs::last_write_time(it->path());
                vector<string> splits;
                split(splits, it->path().filename().string(), boost::is_any_of("_"));
                props["internalid"] = string(splits[1]);
                list.push_back( props );
            }
            ++it;
        }
    }
}

/**
 * Return current date and time
 */
std::string AgoSecurity::getDateTimeString(bool date, bool time, bool withSeparator/*=true*/, std::string fieldSeparator/*="_"*/)
{
    stringstream out;
    if( date )
    {
        out << pt::second_clock::local_time().date().year();
        if( withSeparator )
            out << "/";
        int month = (int)pt::second_clock::local_time().date().month();
        if( month<10 )
            out << "0" << month;
        else
            out << month;
        if( withSeparator )
            out << "/";
        int day = (int)pt::second_clock::local_time().date().day();
        if( day<10 )
            out << "0" << day;
        else
            out << day;
    }

    if( date && time )
    {
        out << fieldSeparator;
    }

    if( time )
    {
        int hours = (int)pt::second_clock::local_time().time_of_day().hours();
        if( hours<10 )
            out << "0" << hours;
        else
            out << hours;
        if( withSeparator )
            out << ":";
        int minutes = (int)pt::second_clock::local_time().time_of_day().minutes();
        if( minutes<10 )
            out << "0" << minutes;
        else
            out << minutes;
        if( withSeparator )
            out << ":";
        int seconds = (int)pt::second_clock::local_time().time_of_day().seconds();
        if( seconds<10 )
            out << "0" << seconds;
        else
            out << seconds;
    }

    return out.str();
}

/**
 * Event handler
 */
void AgoSecurity::eventHandler(std::string subject, qpid::types::Variant::Map content)
{
    bool alarmTriggered = false;
    std::string housemode = "";
    if( securitymap["config"].isVoid() )
    {
        //nothing configured, exit right now
        AGO_DEBUG() << "no config";
        return;
    }

    if( subject=="event.device.statechanged" || subject=="event.security.sensortriggered" )
    {
        //get current housemode
        if( !securitymap["housemode"].isVoid() )
        {
            housemode = securitymap["housemode"].asString();
        }
        else
        {
            //invalid config file
            AGO_ERROR() << "Missing housemode in config file.";
            return;
        }

        AGO_TRACE() << "event received: " << subject << " - " << content;
        //get device uuid
        if( !content["uuid"].isVoid() && !content["level"].isVoid() )
        {
            string uuid = content["uuid"].asString();
            int64_t level = content["level"].asInt64();

            if( level==0 )
            {
                //sensor disabled, nothing to do
                AGO_TRACE() << "Disabled sensor event, event dropped";
                return;
            }
            else if( isAlarmActivated )
            {
                //alarm already running, nothing else to do
                AGO_TRACE() << "Alarm already running, event dropped";
                return;
            }

            //device stated changed, check if it's a monitored one
            qpid::types::Variant::Map config = securitymap["config"].asMap();
            for( qpid::types::Variant::Map::iterator it1=config.begin(); it1!=config.end(); it1++ )
            {
                //check housemode
                std::string hm = it1->first;
                //AGO_TRACE() << " - housemode=" << housemode;
                if( hm==housemode )
                {
                    qpid::types::Variant::List zones = it1->second.asList();
                    for( qpid::types::Variant::List::iterator it2=zones.begin(); it2!=zones.end(); it2++ )
                    {
                        qpid::types::Variant::Map zoneMap = it2->asMap();
                        std::string zoneName = zoneMap["zone"].asString();
                        //AGO_TRACE() << "   - zone=" << zoneName;
                        qpid::types::Variant::List devices = zoneMap["devices"].asList();
                        for( qpid::types::Variant::List::iterator it3=devices.begin(); it3!=devices.end(); it3++ )
                        {
                            //AGO_TRACE() << "     - device=" << *it3;
                            if( (*it3)==uuid )
                            {
                                //zone is triggered
                                AGO_DEBUG() << "housemode[" << housemode << "] is triggered in zone[" << zoneName << "] by device [" << uuid << "]";
                                triggerZone(zoneName, housemode);

                                //trigger alarm only once
                                alarmTriggered = true;
                                break;
                            }
                        }

                        if( alarmTriggered )
                        {
                            //trigger alarm only once
                            break;
                        }
                    }
                }

                if( alarmTriggered )
                {
                    //trigger alarm only once
                    break;
                }
            }
        }
        else
        {
            AGO_DEBUG() << "No uuid for event.device.statechanged " << content;
        }
    }
    else if( subject=="event.environment.timechanged" && !content["minute"].isVoid() && !content["hour"].isVoid() )
    {
        //refresh gateway list every 5 minutes
        if( content["minute"].asInt8()%5==0 )
        {
            refreshAlertGateways();
            refreshDefaultContact();
        }

        //midnight, create new timelapse for new day
        //if( content["hour"].asInt8()==0 && content["minute"].asInt8()==0 )
        if( content["minute"].asInt8()%2==0 )
        {
            restartTimelapses();
        }
    }
    else if( subject=="event.system.devicenamechanged" )
    {
        //handle motion name changed
        bool found = false;
        string name = content["name"].asString();
        string internalid = agoConnection->uuidToInternalId(content["uuid"].asString());

        pthread_mutex_lock(&securitymapMutex);

        //update motion device
        qpid::types::Variant::Map motions = securitymap["motions"].asMap();
        if( !motions[internalid].isVoid() )
        {
            //its a motion device
            found = true;
            qpid::types::Variant::Map motion = motions[internalid].asMap();
            if( !motion["name"].isVoid() )
            {
                motion["name"] = name;
                motions[internalid] = motion;
                securitymap["motions"] = motions;

                //restart motion
                AGO_DEBUG() << " ==> stopMotion";
                stopMotion(internalid);
                AGO_DEBUG() << " ==> launchMotion";
                launchMotion(internalid, motion);
            }
        }
        
        //update timelapse device
        if( !found )
        {
            qpid::types::Variant::Map timelapses = securitymap["timelapses"].asMap();
            if( !timelapses[internalid].isVoid() )
            {
                //its a timelapse device
                found = true;
                qpid::types::Variant::Map timelapse = timelapses[internalid].asMap();
                if( !timelapse["name"].isVoid() )
                {
                    timelapse["name"] = name;
                    timelapses[internalid] = timelapse;
                    securitymap["timelapses"] = timelapses;

                    //restart timelapse
                    AGO_DEBUG() << " ==> stopTimelapse";
                    stopTimelapse(internalid);
                    AGO_DEBUG() << " ==> launchTimelapse";
                    launchTimelapse(internalid, timelapse);
                }
            }
        }

        pthread_mutex_unlock(&securitymapMutex);

        if( found )
        {
            if( variantMapToJSONFile(securitymap, getConfigPath(SECURITYMAPFILE)) )
            {
                AGO_DEBUG() << "Event 'devicenamechanged': motion name changed";
            }
            else
            {
                AGO_ERROR() << "Event 'devicenamechanged': cannot save securitymap";
            }
        }
        else
        {
            //just log an error
            AGO_ERROR() << "devicenamechanged event: no device '" << internalid << "' found in agosecurity. Unable to rename device";
        }
    }
}

/**
 * Command handler
 */
qpid::types::Variant::Map AgoSecurity::commandHandler(qpid::types::Variant::Map content)
{
    AGO_TRACE() << "handling command: " << content;
    qpid::types::Variant::Map returnData;

    std::string internalid = content["internalid"].asString();
    if (internalid == "securitycontroller")
    {
        if (content["command"] == "sethousemode")
        {
            checkMsgParameter(content, "housemode", VAR_STRING);
            checkMsgParameter(content, "pin", VAR_STRING);

            if (checkPin(content["pin"].asString()))
            {
                if( changeHousemode(content["housemode"].asString()) )
                {
                    return responseSuccess("Housemode changed");
                }
                else
                {
                    AGO_ERROR() << "Command 'sethousemode': Cannot write securitymap";
                    return responseFailed("Cannot write config file");
                }
            }
            else
            {
                AGO_ERROR() << "Command 'sethousemode': invalid pin";
                returnData["housemode"] = securitymap["housemode"].asString();
                return responseError("error.security.invalidpin", "Invalid pin specified", returnData);
            }
        }
        else if (content["command"] == "gethousemode")
        {
            if (!(securitymap["housemode"].isVoid()))
            {
                returnData["housemode"]= securitymap["housemode"];
                return responseSuccess(returnData);
            }
            else
            {
                AGO_WARNING() << "Command 'gethousemode': no housemode set";
                return responseError("error.security.housemodenotset", "No housemode set", returnData);
            }
        }
        else if (content["command"] == "triggerzone")
        {
            checkMsgParameter(content, "zone", VAR_STRING);

            std::string zone = content["zone"].asString();
            std::string housemode = securitymap["housemode"];
            TriggerStatus status = triggerZone(zone, housemode);

            switch(status)
            {
                case Ok:
                case OkInactiveZone:
                    return responseSuccess();
                    break;

                case KoAlarmFailed:
                    AGO_ERROR() << "Command 'triggerzone': fail to start alarm thread";
                    return responseError("error.security.alarmthreadfailed", "Failed to start alarm thread");
                    break;

                case KoAlarmAlreadyRunning:
                    return responseSuccess("Alarm thread is already running");
                    break;

                case KoConfigInfoMissing:
                case KoInvalidConfig:
                    AGO_ERROR() << "Command 'triggerzone': invalid configuration file content";
                    return responseError("error.security.invalidconfig", "Invalid config");
                    break;

                default:
                    AGO_ERROR() << "Command 'triggerzone': unknown error";
                    return responseError("error.security.unknown", "Unknown state");
                    break;
            }
        }
        else if (content["command"] == "cancelalarm")
        {
            checkMsgParameter(content, "pin", VAR_STRING);

            if (checkPin(content["pin"].asString()))
            {
                if( isAlarmActivated )
                {
                    if( isSecurityThreadRunning )
                    {
                        //thread is running, cancel it, it will cancel alarm
                        wantSecurityThreadRunning = false;
                        try
                        {
                            securityThread.interrupt();
                            isSecurityThreadRunning = false;
                            AGO_INFO() << "Command 'cancelalarm': alarm cancelled";
                            return responseSuccess("Alarm cancelled");
                        }
                        catch(std::exception& e)
                        {
                            AGO_ERROR() << "Command 'cancelalarm': cannot cancel alarm thread!";
                            return responseError("error.security.alarmthreadcancelfailed", "Cannot cancel alarm thread");
                        }
                    }
                    else
                    {
                        //thread is not running, delay is over and alarm is surely screaming
                        disableAlarm(currentAlarm.zone, currentAlarm.housemode);
                        AGO_INFO() << "Command 'cancelalarm': alarm disabled";
                        return responseSuccess("Alarm disabled");
                    }
                }
                else
                {
                    AGO_ERROR() << "Command 'cancelalarm': no alarm is running :S";
                    return responseError("error.security.alarmthreadcancelfailed", "No alarm running");
                }
            }
            else
            {
                AGO_ERROR() << "Command 'cancelalarm': invalid pin specified";
                return responseError("error.security.invalidpin", "Invalid pin specified");
            }
        }
        else if( content["command"]=="getconfig" )
        {
            if( !securitymap["config"].isVoid() )
            {
                returnData["config"] = securitymap["config"].asMap();
            }
            else
            {
                qpid::types::Variant::Map empty;
                securitymap["config"] = empty;
            }
            returnData["armedMessage"] = "";
            if( !securitymap["armedMessage"].isVoid() )
            {
                returnData["armedMessage"] = securitymap["armedMessage"].asString();
            }
            returnData["disarmedMessage"] = "";
            if( !securitymap["disarmedMessage"].isVoid() )
            {
                returnData["disarmedMessage"] = securitymap["disarmedMessage"].asString();
            }
            returnData["defaultHousemode"] = "";
            if( !securitymap["defaultHousemode"].isVoid() )
            {
                returnData["defaultHousemode"] = securitymap["defaultHousemode"].asString();
            }
            returnData["housemode"] = "";
            if( !securitymap["housemode"].isVoid() )
            {
                returnData["housemode"] = securitymap["housemode"].asString();
            }
            return responseSuccess(returnData);
        }
        else if( content["command"]=="setconfig" )
        {
            checkMsgParameter(content, "config", VAR_MAP);
            checkMsgParameter(content, "armedMessage", VAR_STRING, true);
            checkMsgParameter(content, "disarmedMessage", VAR_STRING, true);
            checkMsgParameter(content, "defaultHousemode", VAR_STRING);
            checkMsgParameter(content, "pin", VAR_STRING);

            if( checkPin(content["pin"].asString()) )
            {
                pthread_mutex_lock(&securitymapMutex);
                qpid::types::Variant::Map newconfig = content["config"].asMap();
                securitymap["config"] = newconfig;
                securitymap["armedMessage"] = content["armedMessage"].asString();
                securitymap["disarmedMessage"] = content["disarmedMessage"].asString();
                securitymap["defaultHousemode"] = content["defaultHousemode"].asString();
                pthread_mutex_unlock(&securitymapMutex);

                if (variantMapToJSONFile(securitymap, getConfigPath(SECURITYMAPFILE)))
                {
                    return responseSuccess();
                }
                else
                {
                    AGO_ERROR() << "Command 'setconfig': cannot save securitymap";
                    return responseError("error.security.setzones", "Cannot save securitymap");
                }
            }
            else
            {
                //invalid pin
                AGO_ERROR() << "Command 'setconfig': invalid pin";
                return responseError("error.security.invalidpin", "Invalid pin specified");
            }
        }
        else if( content["command"]=="checkpin" )
        {
            checkMsgParameter(content, "pin", VAR_STRING);
            if( checkPin(content["pin"].asString() ) )
            {
                return responseSuccess();
            }
            else
            {
                AGO_WARNING() << "Command 'checkpin': invalid pin";
                return responseError("error.security.invalidpin", "Invalid pin specified");
            }
        }
        else if( content["command"]=="setpin" )
        {
            checkMsgParameter(content, "pin", VAR_STRING);
            checkMsgParameter(content, "newpin", VAR_STRING);
            //check pin
            if (checkPin(content["pin"].asString()))
            {
                if( setPin(content["newpin"].asString()) )
                {
                    return responseSuccess();
                }   
                else
                {
                    AGO_ERROR() << "Command 'setpin': unable to save pin";
                    return responseError("error.security.setpin", "Unable to save new pin code");
                }
            }
            else
            {
                //wrong pin specified
                AGO_WARNING() << "Command 'setpin': invalid pin";
                return responseError("error.security.invalidpin", "Invalid pin specified");
            }
        }
        else if( content["command"]=="getalarmstate" )
        {
            returnData["alarmactivated"] = isAlarmActivated;
            return responseSuccess(returnData);
        }
        else if( content["command"]=="addtimelapse" )
        {
            checkMsgParameter(content, "uri", VAR_STRING);
            checkMsgParameter(content, "fps", VAR_INT32);
            checkMsgParameter(content, "codec", VAR_STRING);
            checkMsgParameter(content, "enabled", VAR_BOOL);

            //check if timelapse already exists or not
            pthread_mutex_lock(&securitymapMutex);
            string uri = content["uri"].asString();
            qpid::types::Variant::Map timelapses = securitymap["timelapses"].asMap();
            for( qpid::types::Variant::Map::iterator it=timelapses.begin(); it!=timelapses.end(); it++ )
            {
                qpid::types::Variant::Map timelapse = it->second.asMap();
                if( !timelapse["uri"].isVoid() )
                {
                    string timelapseUri = timelapse["uri"].asString();
                    if( timelapseUri==uri )
                    {
                        //uri already exists, stop here
                        pthread_mutex_unlock(&securitymapMutex);
                        return responseError("error.security.addtimelapse", "Timelapse already exists");
                    }
                }
            }

            //fill new timelapse
            qpid::types::Variant::Map timelapse;
            fillTimelapse(&timelapse, &content);

            //and save it
            string internalid = generateUuid();
            timelapses[internalid] = timelapse;
            securitymap["timelapses"] = timelapses;
            pthread_mutex_unlock(&securitymapMutex);
            if( variantMapToJSONFile(securitymap, getConfigPath(SECURITYMAPFILE)) )
            {
                AGO_DEBUG() << "Command 'addtimelapse': timelapse added " << timelapse;

                //and finally launch timelapse thread
                launchTimelapse(internalid, timelapse);

                qpid::types::Variant::Map result;
                result["internalid"] = internalid;
                return responseSuccess("Timelapse added", result);
            }
            else
            {
                AGO_ERROR() << "Command 'addtimelapse': cannot save securitymap";
                return responseError("error.security.addtimelapse", "Cannot save config");
            }
        }
        /*else if( content["command"]=="removetimelapse" )
        {
            bool found = false;

            checkMsgParameter(content, "uri", VAR_STRING);
            
            //search and destroy specified timelapse
            pthread_mutex_lock(&securitymapMutex);
            string uri = content["uri"].asString();
            qpid::types::Variant::List timelapses = securitymap["timelapses"].asList();
            for( qpid::types::Variant::List::iterator it=timelapses.begin(); it!=timelapses.end(); it++ )
            {
                qpid::types::Variant::Map timelapse = it->asMap();
                if( !timelapse["uri"].isVoid() )
                {
                    string timelapseUri = timelapse["uri"].asString();
                    if( timelapseUri==uri )
                    {
                        //timelapse found
                        found = true;
                        
                        //stop running timelapse thread
                        timelapseThreads[uri]->interrupt();

                        //and remove it from config
                        timelapses.erase(it);
                        securitymap["timelapses"] = timelapses;
                        break;
                    }
                }
            }
            pthread_mutex_unlock(&securitymapMutex);

            if( found )
            {
                if( variantMapToJSONFile(securitymap, getConfigPath(SECURITYMAPFILE)) )
                {
                    AGO_DEBUG() << "Command 'removetimelapse': timelapse remove";
                    return responseSuccess("Timelapse removed");
                }
                else
                {
                    AGO_ERROR() << "Command 'removetimelapse': cannot save securitymap";
                    return responseError("error.security.removetimelapse", "Cannot save config");
                }
            }
            else
            {
                return responseError("error.security.removetimelapse", "Specified timelapse was not found");
            }
        }*/
        else if( content["command"]=="gettimelapses" )
        {
            qpid::types::Variant::List timelapses;
            getRecordings("timelapse_", timelapses);
            returnData["timelapses"] = timelapses;
            return responseSuccess(returnData);
        }
        else if( content["command"]=="addmotion" )
        {
            checkMsgParameter(content, "uri", VAR_STRING);
            checkMsgParameter(content, "sensitivity", VAR_INT32);
            checkMsgParameter(content, "deviation", VAR_INT32);
            checkMsgParameter(content, "bufferduration", VAR_INT32);
            checkMsgParameter(content, "onduration", VAR_INT32);
            checkMsgParameter(content, "recordduration", VAR_INT32);
            checkMsgParameter(content, "enabled", VAR_BOOL);

            //check values
            if( content["recordduration"].asInt32()>=content["onduration"].asInt32() )
            {
                AGO_WARNING() << "Addmotion: record duration must be lower than on duration. Record duration forced to on duration.";
                content["recordduration"] = content["onduration"].asInt32() - 1;
            }
            if( content["bufferduration"].asInt32()>=content["recordduration"].asInt32() )
            {
                AGO_WARNING() << "Addmotion: buffer duration must be lower than record duration. Buffer duration forced to record duration.";
                content["bufferduration"] = content["recordduration"].asInt32() - 1;
            }

            //check if motion already exists or not
            pthread_mutex_lock(&securitymapMutex);
            string uri = content["uri"].asString();
            qpid::types::Variant::Map motions = securitymap["motions"].asMap();
            for( qpid::types::Variant::Map::iterator it=motions.begin(); it!=motions.end(); it++ )
            {
                qpid::types::Variant::Map motion = it->second.asMap();
                if( !motion["uri"].isVoid() )
                {
                    string motionUri = motion["uri"].asString();
                    if( motionUri==uri )
                    {
                        //uri already exists, stop here
                        pthread_mutex_unlock(&securitymapMutex);
                        return responseError("error.security.addmotion", "Motion already exists");
                    }
                }
            }

            //fill new motion
            qpid::types::Variant::Map motion;
            fillMotion(&motion, &content);

            //and save it
            string internalid = generateUuid();
            motions[internalid] = motion;
            securitymap["motions"] = motions;
            pthread_mutex_unlock(&securitymapMutex);
            if( variantMapToJSONFile(securitymap, getConfigPath(SECURITYMAPFILE)) )
            {
                AGO_DEBUG() << "Command 'addmotion': motion added " << motion;

                //and finally launch motion thread
                launchMotion(internalid, motion);

                qpid::types::Variant::Map result;
                result["internalid"] = internalid;
                return responseSuccess("Motion added", result);
            }
            else
            {
                AGO_ERROR() << "Command 'addmotion': cannot save securitymap";
                return responseError("error.security.addmotion", "Cannot save config");
            }
        }
        /*else if( content["command"]=="removemotion" )
        {
            bool found = false;

            checkMsgParameter(content, "uri", VAR_STRING);
            
            //search and destroy specified motion
            pthread_mutex_lock(&securitymapMutex);
            string uri = content["uri"].asString();
            qpid::types::Variant::Map motions = securitymap["motions"].asMap();
            for( qpid::types::Variant::List::iterator it=motions.begin(); it!=motions.end(); it++ )
            {
                qpid::types::Variant::Map motion = it->asMap();
                if( !motion["uri"].isVoid() )
                {
                    string motionUri = motion["uri"].asString();
                    if( motionUri==uri )
                    {
                        //motion found
                        found = true;
                        
                        //stop running motion thread
                        stopMotion(motion);

                        //and remove it from config
                        motions.erase(it);
                        securitymap["motions"] = motions;
                        break;
                    }
                }
            }
            pthread_mutex_unlock(&securitymapMutex);

            if( found )
            {
                if( variantMapToJSONFile(securitymap, getConfigPath(SECURITYMAPFILE)) )
                {
                    AGO_DEBUG() << "Command 'removemotion': motion removed";
                    return responseSuccess("Motion removed");
                }
                else
                {
                    AGO_ERROR() << "Command 'removemotion': cannot save securitymap";
                    return responseError("error.security.removemotion", "Cannot save config");
                }
            }
            else
            {
                return responseError("error.security.removemotion", "Specified motion was not found");
            }
        }*/
        else if( content["command"]=="getmotions" )
        {
            qpid::types::Variant::List motions;
            getRecordings("motion_", motions);
            returnData["motions"] = motions;
            return responseSuccess(returnData);
        }
        else if( content["command"]=="getrecordingsconfig" )
        {
            qpid::types::Variant::Map config = securitymap["recordings"].asMap();
            returnData["config"] = config;
            return responseSuccess(returnData);
        }
        else if( content["command"]=="setrecordingsconfig" )
        {
            checkMsgParameter(content, "timelapseslifetime", VAR_INT32);
            checkMsgParameter(content, "motionslifetime", VAR_INT32);

            pthread_mutex_lock(&securitymapMutex);
            qpid::types::Variant::Map config = securitymap["recordings"].asMap();
            config["timelapseslifetime"] = content["timelapseslifetime"].asInt32();
            config["motionslifetime"] = content["motionslifetime"].asInt32();
            pthread_mutex_lock(&securitymapMutex);

            if( variantMapToJSONFile(securitymap, getConfigPath(SECURITYMAPFILE)) )
            {
                AGO_DEBUG() << "Command 'setrecordingsconfig': recordings config stored";
                return responseSuccess();
            }
            else
            {
                AGO_ERROR() << "Command 'setrecordingsconfig': cannot save securitymap";
                return responseError("error.security.setrecordingsconfig", "Cannot save config");
            }
        }

        return responseUnknownCommand();
    }
    else
    {
        //handle motion/timelapse devices
        if( content["command"]=="enable" )
        {
            /*bool found = false;

            checkMsgParameter(content, "enabled", VAR_BOOL);
            bool enabled = content["enabled"].asBool();

            if( found )
            {
                if( variantMapToJSONFile(securitymap, getConfigPath(SECURITYMAPFILE)) )
                {
                    if( enabled )
                    {
                        AGO_DEBUG() << "Command 'enablemotion': motion enabled";
                        return responseSuccess("Motion enabled");
                    }
                    else
                    {
                        AGO_DEBUG() << "Command 'enablemotion': motion disabled";
                        return responseSuccess("Motion disabled");
                    }
                }
                else
                {
                    AGO_ERROR() << "Command 'enablemotion': cannot save securitymap";
                    return responseError("error.security.enablemotion", "Cannot save config");
                }
            }
            else
            {
                return responseError("error.security.enablemotion", "Specified motion was not found");
            }*/
        }
        else if( content["command"]=="enablerecording" )
        {
        }

        return responseUnknownCommand();
    }

    // We have no devices registered but our own
    throw std::logic_error("Should not go here");
}

void AgoSecurity::setupApp()
{
    //init
    pthread_mutex_init(&alertGatewaysMutex, NULL);
    pthread_mutex_init(&contactsMutex, NULL);
    pthread_mutex_init(&securitymapMutex, NULL);
    pthread_mutex_init(&frameProvidersMutex, NULL);

    //load config
    securitymap = jsonFileToVariantMap(getConfigPath(SECURITYMAPFILE));
    //add missing sections if necessary
    if( securitymap["timelapses"].isVoid() )
    {
        qpid::types::Variant::Map timelapses;
        securitymap["timelapses"] = timelapses;
        variantMapToJSONFile(securitymap, getConfigPath(SECURITYMAPFILE));
    }
    if( securitymap["motions"].isVoid() )
    {
        qpid::types::Variant::Map motions;
        securitymap["motions"] = motions;
        variantMapToJSONFile(securitymap, getConfigPath(SECURITYMAPFILE));
    }
    if( securitymap["recordings"].isVoid() )
    {
        qpid::types::Variant::Map recordings;
        recordings["timelapseslifetime"] = 7;
        recordings["motionslifetime"] = 14;
        securitymap["recordings"] = recordings;
        variantMapToJSONFile(securitymap, getConfigPath(SECURITYMAPFILE));
    }
    AGO_DEBUG() << "Loaded securitymap: " << securitymap;
    std::string housemode = securitymap["housemode"];
    AGO_DEBUG() << "Current house mode: " << housemode;
    agoConnection->setGlobalVariable("housemode", housemode);

    //get available alert gateways
    refreshAlertGateways();
    refreshDefaultContact();

    //finalize
    agoConnection->addDevice("securitycontroller", "securitycontroller");
    addCommandHandler();
    addEventHandler();

    //launch timelapse threads
    launchTimelapses();

    //launch motion threads
    launchMotions();
}

void AgoSecurity::cleanupApp()
{
    //stop processes
    stopProcess = true;

    //wait for timelapse threads stop
    for( std::map<std::string, boost::thread*>::iterator it=timelapseThreads.begin(); it!=timelapseThreads.end(); it++ )
    {
        stopTimelapse(it->first);
        /*(it->second)->interrupt();
        (it->second)->join();*/
    }

    //wait for motion threads stop
    for( std::map<std::string, boost::thread*>::iterator it=motionThreads.begin(); it!=motionThreads.end(); it++ )
    {
        stopMotion(it->first);
        /*(it->second)->interrupt();
        (it->second)->join();*/
    }

    //close frame providers
    for( std::map<std::string, AgoFrameProvider*>::iterator it=frameProviders.begin(); it!=frameProviders.end(); it++ )
    {
        (it->second)->stop();
    }
}

AGOAPP_ENTRY_POINT(AgoSecurity);

