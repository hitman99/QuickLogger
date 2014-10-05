/* 
 * File:   QUICKLogger.cpp
 * Author: hitman
 * 
 * Created on January 7, 2013, 11:31 AM
 */

#include "QuickLogger.h"
#include <string>
#include <iostream>
#include <fstream>
#include <ctime>
#include <sys/time.h>
#include <iomanip>
#include <sstream>
#include <vector>
#include <atomic>
#include <mutex>
#include <chrono>
#include <map>
#include <queue>
#include <unordered_map>
#include <thread>
#include <algorithm>

using namespace std::chrono;

// Actual IMPLementation class 
class QuickLogger::impl {
public:
    impl(string path, string name, string rolloverperiod);
    ~impl();
    void Log(const string *message, 
             const string *loglevel, 
             const string *component);
    void Setloglevels(string levels);
    void Setfields(string fields);
    void Halt();
    void Tokenize(string text, string delims, vector<string> & tokens);
    long Getbufferoverflows();
    string Getfilename();
    void Toggleloglevel(const string *level, const bool *enabled);
    void Setflushfrequency(unsigned int freq);
    void Setbuffersize(unsigned int size);
private:
    // variables
    // path where log file(s) will be stored
    string path;
    // log name, result will be: QL_<name>_YYYYMMDDHHmmSS.log.csv
    string name;
    // absolute filename
    string filename;
    // order in which messages are written in the file
    std::vector<int> fieldorder;
    //available fields to be used
    std::map<string, int> availablefields;
    // actual log levels
    std::unordered_map<string, bool> loglevels;
    // log buffer size in messages
    unsigned int buffersize;
    // how often flush buffer to disk
    std::chrono::milliseconds flushfrequency;
    // rollover period definition
    std::string rolloverperiod;
    // tm struct containing rollovertime. 00:00:00 by default
    std::tm rollovertime;
    // counter of buffer overflows
    std::atomic<long> bufferoverflowcount;
    // current file handle
    ofstream filehandle;
    // internal message structure containing timestamp, log level, component 
    // name and actual message itself.
    struct M{
        M(string t, string ll, string c, string m) :
        timestamp(t), loglevel(ll), component(c), message(m) { };
        string timestamp;
        string loglevel;
        string component;
        string message;
    };
    // primary buffer. Used most of the time
    queue<M> primarybuffer;
    // secondary buffer. Used only during flushing from the primary buffer.
    queue<M> secondarybuffer;
    // a swich to redirect the messages to secondary buffer.
    // false - primary buffer used, true - secondary buffer used
    std::atomic<bool> redirectflow;
    // buffer mutex. Prevents two threads from writing messages at the same time
    // because I was too lazy to implement lock free queue :)
    std::mutex _m_buffer;
    // file handle mutex. Will be locked only during rollover.
    std::mutex _m_ofstream;
    // thread that actually writes messages to file
    std::thread flush_thread;
    // thread that rolls over the files
    std::thread rollover_thread;
    // stop all threads and gracefully exit. false - don't stop, true - stop.
    std::atomic<bool> thread_stop;
    // possible timeframes for rollover period
    std::map<string, int> timeframes;
    //----------------------  methods  ------------------------------------
    // returns time as a string in format YYYY-MM-DD HH:mm:ss.nanoseconds
    string GetTime(std::string format = "YMD");
    // sets up everything in the beginning
    void Initialize();
    // make string from something else
    template <typename T>
    std::string stringify(T x);
    // converts integer to string
    int Integerify(string);
    // runs in separate thread
    void Autorollover();
    // maps the log level with defined ones and returns the id, if found
    unsigned int Maploglevel(string level);
    //manages flushing to the disk - runs in separate thread
    void Flush();
    // actually flushes messages to the disk
    void SinkPipe(queue<M> * buffer);
    //defines available timeframe keywords
    void Initializetimeframes();
    //calculates seconds till the next rollover
    std::chrono::seconds Calculaterolloverperiod(pair<int, map<string, int>::iterator> p);
    //parses rollover period defined by string
    std::pair<int, std::map<string, int>::iterator> Parserolloverperiod();
    /**
     * Generates filename and stores it in filename string
     */
    void Generatefilename();
    /**
     * Directly store a message to the current file. Not-buffered.
     * Will lock ofstream mutex. 
     * Use only for internal message logging.
     * @param message
     */
    void DirectLog(string message); 
};
//--------------------------------------------------------------------------
//Interface wrapper
QuickLogger::QuickLogger(string path, string name, string rolloverperiod) : 
        PrivateImpl( new impl(path, name, rolloverperiod)) { }
// Actual constructor
QuickLogger::impl::impl(string path, string name, string rolloverperiod) :
        // initialization list
        path(path),
        name(name),
        rolloverperiod(rolloverperiod),
        flushfrequency(10),
        thread_stop(false),
        redirectflow(false),
        bufferoverflowcount(0)
{
    this->Generatefilename(); 
    this->Initialize();
}
//--------------------------------------------------------------------------
//Actual destructor
QuickLogger::impl::~impl(){
    
}
//--------------------------------------------------------------------------
QuickLogger::~QuickLogger() {
    this->PrivateImpl->Halt();
}
//--------------------------------------------------------------------------
template <typename T>
string QuickLogger::impl::stringify(T x){
    std::ostringstream o;
    if (!(o << x))
        return "";
    return o.str();
}
//--------------------------------------------------------------------------
void QuickLogger::impl::Initialize(){
    // open log file, append if already exists
    // throws, not properly initialized if so.
    try{
        this->filehandle.open(this->filename.c_str(), ios::app);
    }
    catch(std::ofstream::failure e){
        cerr << "Failed to open file " + 
                this->filename + ": " + string(e.what()) << endl;
    }
    // available log fields
    availablefields.insert(pair<string, int>("TIME",            0));
    availablefields.insert(pair<string, int>("LEVEL",           1));
    availablefields.insert(pair<string, int>("COMPONENT",       2));
    availablefields.insert(pair<string, int>("MESSAGE",         3));
    // default log levels
    this->Setloglevels("FATAL,ERROR,WARNING,INFO,DEBUG");
    // default order of fields
    this->Setfields("TIME,LEVEL,COMPONENT,MESSAGE");
    // default buffer size
    this->buffersize = 1000;
    /***************************** Time-Frames ********************************/
    //with multiplier
    timeframes.insert(pair<string, int>("second",   0));
    timeframes.insert(pair<string, int>("minute",   1));
    timeframes.insert(pair<string, int>("hour",     2));
    timeframes.insert(pair<string, int>("day",      3));
    timeframes.insert(pair<string, int>("week",     4));
    timeframes.insert(pair<string, int>("month",    5));
    timeframes.insert(pair<string, int>("year",     6));
    // without multiplier
    timeframes.insert(pair<string, int>("Monday",   7));
    timeframes.insert(pair<string, int>("Tuesday",  8));
    timeframes.insert(pair<string, int>("Wednesday",9));
    timeframes.insert(pair<string, int>("Thursday", 10));
    timeframes.insert(pair<string, int>("Friday",   11));
    timeframes.insert(pair<string, int>("Saturday", 12));
    timeframes.insert(pair<string, int>("Sunday",   13));
    /**************************************************************************/  
    //initialize default time (00:00:00)
    this->rollovertime.tm_hour = 0;
    this->rollovertime.tm_min = 0;
    this->rollovertime.tm_sec = 0;
    this->flush_thread = std::thread(&QuickLogger::impl::Flush, this);
    //this->flush_thread.detach();
    this->rollover_thread = std::thread(&QuickLogger::impl::Autorollover, this);
    //this->rollover_thread.detach();
}
//--------------------------------------------------------------------------
void QuickLogger::Log(string message, string loglevel, string component){
    this->PrivateImpl->Log( &message, &loglevel, &component);
}
//--------------------------------------------------------------------------
void QuickLogger::impl::Log(const string * message, const string * loglevel, const string * component){
    // map the log level here
    //lock mutex
    std::lock_guard<std::mutex> lock(_m_buffer);
    queue<M> * buff;
    if(!this->redirectflow.load()){
        buff = &this->primarybuffer;
        //cout << "Logging to primary buffer: " << *message << endl;
    }
    else{
        //cout << "Logging to secondary buffer: " << *message << endl;
        buff = &this->secondarybuffer;
    }
    if(buff->size() < this->buffersize){
        //auto p = this->loglevels.find(*loglevel);
        buff->push(M(this->GetTime("Y-M-D h:m:s.l"), *loglevel, *component, *message));
    }
    else
        this->bufferoverflowcount++;
}
//--------------------------------------------------------------------------
/**
 * Generates current human readable timestamp
 * @param format - boolean 
 *        Defines format of the time. 
 *              Y - year 
 *              M - month 
 *              D - day
 *              h - hour
 *              m - minute
 *              s - second
 *              l - microseconds 
 *      Any other character will be outputed as-is.
 * @return string - timestamp
 */
string QuickLogger::impl::GetTime(std::string format){
    time_t t;
    timeval tv;
    tm * now;
    gettimeofday(&tv, NULL);
    t = tv.tv_sec;
    now = localtime(&t);
    ostringstream ss;
    ss << std::setfill('0');
    for(auto it = format.begin(); it != format.end(); it++){
        switch(*it){
            case 'Y':
                ss << (now->tm_year + 1900);
                break;
            case 'M':
                ss << std::setw(2) << (now->tm_mon + 1);
                break;
            case 'D':
                ss << std::setw(2) << now->tm_mday;
                break;
            case 'h':
                ss << std::setw(2) << now->tm_hour;
                break;
            case 'm':
                ss << std::setw(2) << now->tm_min;
                break;
            case 's':
                ss << std::setw(2) << now->tm_sec;
                break;
            case 'l':
                ss << std::setw(6) << tv.tv_usec;
                break;
            default :
                ss << std::setw(1) <<  *it;
                break;
        }
    }
    return ss.str();
}
//--------------------------------------------------------------------------
/**
 * Runs in separate thread. All it does is rolls over the file after timeout of
 * value assigned to rolloverperiod occurs.
 */
void QuickLogger::impl::Autorollover(){
    pair<int, map<string, int>::iterator> p = this->Parserolloverperiod();
    chrono::seconds s = this->Calculaterolloverperiod(p);
    /* check every 30 ms if the time is right to do a rollover
     * this is to allow thread to exit gracefully and main thread to be able
     * to join it */
    while(!this->thread_stop){
        std::this_thread::sleep_for(std::chrono::seconds(1));
        s--;
        if( s.count() <= 0){
            /* not locking ofstream mutex here, because DirectLog locks 
             * it internally
             */ 
            // store current buffer overflow counter in the file
            this->DirectLog("Buffer overflows for this file: " + 
                             this->stringify(this->bufferoverflowcount.load()));
            // reset buffer overflows for this file
            this->bufferoverflowcount.store(0);
            // lock ofstream mutex
            _m_ofstream.lock();
            // close the current file
            this->filehandle.close();
            // generate new filename
            this->Generatefilename();
            // open new file
            try{
                this->filehandle.open(this->filename.c_str(), ios::app);
            }
            catch(std::ofstream::failure e){
                throw "Failed to open file " + this->filename + ": " + 
                      string(e.what());
            }
            // unlock
            _m_ofstream.unlock();
            // calculate next rollover
            s = this->Calculaterolloverperiod(p);
        }
    }
}
//--------------------------------------------------------------------------
void QuickLogger::impl::Halt(){
    this->thread_stop = true;
    this->flush_thread.join();
    this->rollover_thread.join();
}
//-------------------------------------------------------------------------
void QuickLogger::impl::Flush(){
    while(!this->thread_stop){
        this->redirectflow.store(true);
        //flush primary buffer
        this->SinkPipe(&primarybuffer);
        //redirect flow back to the primary buffer
        this->redirectflow.store(false);
        this->SinkPipe(&secondarybuffer);
        std::this_thread::sleep_for(this->flushfrequency);
    }
    this->redirectflow.store(true);
    //flush primary buffer
    this->SinkPipe(&primarybuffer);
    //redirect flow back to the primary buffer
    this->redirectflow.store(false);
    this->SinkPipe(&secondarybuffer);
    this->DirectLog("Buffer overflows for this file: " + 
                             this->stringify(this->bufferoverflowcount.load()));
    this->filehandle.close();
}
//--------------------------------------------------------------------------
void QuickLogger::impl::SinkPipe(queue<M> * buffer){
    std::lock_guard<std::mutex> lock(_m_ofstream);
    auto p = this->loglevels.begin();
    while(!buffer->empty()){
        p = this->loglevels.find(buffer->front().loglevel);
        // flushing only if log level is unknown, or enabled
        if(p == this->loglevels.end() || (*p).second){
            // go through fieldorder vector and write message to the stream
            // in the correct order
            try{
                for(auto i = fieldorder.begin(); i != fieldorder.end(); i++){
                    //value should be always found in th map!
                    switch(*i){
                        // time
                        case 0:
                            this->filehandle << buffer->front().timestamp;
                            break;
                        // log level
                        case 1:
                            this->filehandle << buffer->front().loglevel;
                            break;
                        // component
                        case 2:
                            this->filehandle << buffer->front().component;
                            break;
                        // message
                        case 3:
                            this->filehandle << buffer->front().message;
                            break;
                    }

                    if(std::next(i) != fieldorder.end()){
                        // configured delimiter could be used
                        this->filehandle << ",";
                    }
                }
                this->filehandle << endl;
            }
            catch(std::ofstream::failure e){
                cerr << "Failed to open file " + 
                      this->filename + ": " + string(e.what()) << endl;
            }
        }
        buffer->pop();
    }
}
//--------------------------------------------------------------------------
/**
 * Parses rolover period defined by rolloverperiod string.
 * @return std::pair<int, iterator>
 *               first: interval multiplier 
 *              second: iterator for timeframes map
 */
std::pair<int, map<string, int>::iterator> QuickLogger::impl::Parserolloverperiod(){
    //parse the string
    std::string::size_type pos = 0, timepos = 0;
    int multiplier = 1;
    //in case of multiplier
    pos = rolloverperiod.find_first_of(" ", pos);
    // if space is found
    if(pos != string::npos){
        // if multiplier is parsed as zero (in case of non-digit)
        // then reset the position to npos
        if((multiplier = Integerify(rolloverperiod.substr(0, pos))) == 0){
            pos = string::npos;
            multiplier = 1;
        }
    }
    // if space is not found, reset position to 0
    if(pos == string::npos)
        pos = 0;
    else{
        pos++;
    }
    // search for the time definition prefixed with '@' symbol
    timepos = rolloverperiod.find_first_of("@", pos);
    if(timepos != string::npos){
        vector<string> tokens;
        Tokenize(rolloverperiod.substr(timepos + 1), ":", tokens);
        for(int u = 0; u < tokens.size(); u++){
            switch(u){
                case 0:
                    this->rollovertime.tm_hour = this->Integerify(tokens.at(u));
                    break;
                case 1:
                    this->rollovertime.tm_min = this->Integerify(tokens.at(u));
                    break;
                case 2:
                    this->rollovertime.tm_sec = this->Integerify(tokens.at(u));
                    break;
            }
        }
    }
    // extract period definition
    std::string periodname = rolloverperiod.substr(pos,
                          (timepos != string::npos)? (timepos - pos) : timepos);
    //cout << "multiplier = " << multiplier << " periodname=" << periodname << endl;
    std::string::size_type tmplen = periodname.length();
    // parsing time period
    auto p = timeframes.end();
    int maxiterations = 0;
    while ( p == timeframes.end() && maxiterations < 3){
        p = timeframes.find(periodname.substr(0, tmplen - maxiterations++));
    }
    if ( p == timeframes.end() ){
        // one day by default
        p = timeframes.find("second");
        multiplier = 86400;
    }
    return pair<int, map<string, int>::iterator>(multiplier, p);
}
//--------------------------------------------------------------------------
int QuickLogger::impl::Integerify(string num){
    int x = 0;
    try{
        istringstream(num) >> x;
    }
    catch(...){
        
    }
    return x;
}
//--------------------------------------------------------------------------
std::chrono::seconds QuickLogger::impl::Calculaterolloverperiod(
                                pair<int, map<string, int>::iterator> p){
    // get current time 
    std::time_t now = std::time(NULL);
    // convert to tm structure with local time (server time)
    std::tm tm = *std::localtime(&now);
    // this will be returned eventually
    std::chrono::seconds secs;
    // set to defaults
    tm.tm_hour = this->rollovertime.tm_hour;
    tm.tm_min = this->rollovertime.tm_min;
    tm.tm_sec = this->rollovertime.tm_sec;
    // find out which case is it, here we use iterator from timeframes map
    switch((*p.second).second){
        default:
        case 0: // seconds
            secs = std::chrono::seconds (p.first);
            break;
        case 1: // minutes
            secs = std::chrono::minutes (p.first);
            break;
        case 2: // hours
            secs = std::chrono::hours (p.first);
            break;
        case 3: // days
            //secs = std::chrono::hours (p.first) * 24;
            tm.tm_mday += p.first;
            //cout << "secs=" << tm.tm_sec << endl;
            secs = duration_cast<seconds>
                                (system_clock::from_time_t(mktime(&tm)) - 
                                 system_clock::now());
            break;
        case 4: // weeks
            //secs = std::chrono::hours (p.first) * 24 * 7;
            tm.tm_mday += p.first * 7;
            secs = duration_cast<seconds>
                                (system_clock::from_time_t(mktime(&tm)) - 
                                 system_clock::now());
            break;
        case 5: // months
            tm.tm_mon++;
            tm.tm_mday = 1;
            secs = duration_cast<seconds>
                                (system_clock::from_time_t(mktime(&tm)) - 
                                 system_clock::now());
            break;
        case 6:
            break;
        case 7:
        case 8:
        case 9:
        case 10:
        case 11:
        case 12:
        case 13:
            // since case the switch key holds the value from 7 to 13, 
            // by subtracting 7, we get offset from the Monday which is useful
            // if the weekday for this week is passed (runtime case)
            if(tm.tm_wday >  ((*p.second).second - 7)){
                tm.tm_mday += (1 + (7 - tm.tm_wday) + ((*p.second).second - 7));
            }
            // if the weekday is still in the future (only at startup)
            else{
                tm.tm_mday += (1 + ((*p.second).second - 7) - tm.tm_wday);
            }
            //tm.tm_wday = 0;
            secs = duration_cast<seconds>
                                (system_clock::from_time_t(mktime(&tm)) - 
                                 system_clock::now());
            break;
    }
    // correction
    //secs += std::chrono::seconds(1);
    now = system_clock::to_time_t(system_clock::now() + secs);
    //printf("Future time will be: %s", ctime(&now));
    return secs;
}
//--------------------------------------------------------------------------
// Wrapper for impl class setter
void QuickLogger::Setfields(string fields){
    this->PrivateImpl->Setfields(fields);
}
//--------------------------------------------------------------------------
void QuickLogger::impl::Setfields(string fields){
    vector<string> tokens;
    // explode string by comma
    this->Tokenize(fields, ",", tokens);
    //filter all fields by only available ones
    /*std::remove_if(tokens.begin(), tokens.end(), [this](string s){
        return ((this->availablefields.find(s) == string::npos) ? true : false);
    });*/
    // check if there's at least 1 field configured
    if(tokens.size() > 0){
        //this->fields.clear();
        this->fieldorder.clear();
        std::map<string, int>::iterator p;
        for(auto i = tokens.begin(); i != tokens.end(); i++){
            p = this->availablefields.find((*i));
            if(p != this->availablefields.end()){
                //this->fields.insert(pair<int, string>((*p).second,    (*i)));
                this->fieldorder.push_back((*p).second);
            }
        }
    }
}
//--------------------------------------------------------------------------
void QuickLogger::Setloglevels(string levels){
    this->PrivateImpl->Setloglevels(levels);
}
//--------------------------------------------------------------------------
void QuickLogger::impl::Setloglevels(string levels){
    vector<string> tokens;
    // explode string by comma
    this->Tokenize(levels, ",", tokens);
    if(tokens.size() > 0){
        for(auto i = tokens.begin(); i != tokens.end(); i++){
            this->loglevels.insert(pair<string, int>((*i), true));
        }
    }
}
//--------------------------------------------------------------------------
/**
 * Tokenizes string by delimiters and stores them in vector
 * @param text - input string
 * @param delimiters - delimiters
 * @param tokens - vector to store the tokens
 */
void QuickLogger::impl::Tokenize( string text, string delimiters,
                                  vector<string> &tokens){
    std::string::size_type pos, last_pos = 0;
    pos = text.find_first_not_of(delimiters,0);
    while(pos != string::npos && last_pos != string::npos){
        last_pos = text.find_first_of(delimiters, pos);
        tokens.push_back(text.substr(pos, last_pos - pos));
        pos = text.find_first_not_of(delimiters, last_pos);
        while((text.find_first_of(delimiters, last_pos+1) - last_pos) == 1){
            tokens.push_back("");
            last_pos++;
        }
        //if delimiter is the last symbol in the string
    }
    if(delimiters.find((*text.rbegin())) != string::npos)
        tokens.push_back("");
}
//--------------------------------------------------------------------------
long QuickLogger::impl::Getbufferoverflows(){
    return this->bufferoverflowcount.load();
}
//--------------------------------------------------------------------------
// Interface wrapper for class impl getter for Getbufferoverflows
long QuickLogger::Getbufferoverflows(){
    return this->PrivateImpl->Getbufferoverflows();
}
//--------------------------------------------------------------------------
void QuickLogger::impl::Generatefilename(){
    this->filename = this->path + "/QL_" + this->name + "_" + 
                     this->GetTime("YMD") + ".log.csv";
}
//--------------------------------------------------------------------------
string QuickLogger::Getfilename(){
    return this->PrivateImpl->Getfilename();
}
//--------------------------------------------------------------------------
string QuickLogger::impl::Getfilename(){
    return this->filename;
}
//--------------------------------------------------------------------------
/**
 * writes message to the file avoiding the buffer. Use for system messages only!
 * @param message
 */
void QuickLogger::impl::DirectLog(string message){
    std::lock_guard<std::mutex> lock(_m_ofstream);
    for(auto i = fieldorder.begin(); i != fieldorder.end(); i++){
        //value should be always found in th map!
        switch(*i){
            // time
            case 0:
                this->filehandle << this->GetTime("Y-M-D h:m:s.l");
                break;
            // log level
            case 1:
                this->filehandle << "INFO";
                break;
            // component
            case 2:
                this->filehandle << "QuickLogger";
                break;
            // message
            case 3:
                this->filehandle << message;
                break;
        }
        if(std::next(i) != fieldorder.end()){
            // configured delimiter could be used
            this->filehandle << ",";
        }
    }
    this->filehandle << endl;
}
//--------------------------------------------------------------------------
// Wrapper for impl function with the same name
void QuickLogger::Toggleloglevel(string level, bool enabled){
    this->PrivateImpl->Toggleloglevel(&level, &enabled);
}
//--------------------------------------------------------------------------
void QuickLogger::impl::Toggleloglevel(const string *level, 
                                       const bool *enabled){
    // Nasty hack, locking _m_ofstream mutex, to blovk the SinkPipe, even
    // this mutes has nothing to do with the log level toggling.
    // By doing this, use of additional mutex is not required which saves
    // CPU time (no need to lock additional mutexes)
    std::lock_guard<std::mutex> lock(_m_ofstream);
    auto p = this->loglevels.find(*level);
    if(p != this->loglevels.end()){
        (*p).second = *enabled;
    }
}
//--------------------------------------------------------------------------
// Wrapper for the function in class impl with the same name
void QuickLogger::Setflushfrequency(unsigned int flushfreq){
    this->PrivateImpl->Setflushfrequency(flushfreq);
}
//--------------------------------------------------------------------------
void QuickLogger::impl::Setflushfrequency(unsigned int freq){
    this->flushfrequency = std::chrono::milliseconds(freq);
}
//--------------------------------------------------------------------------
// Wrapper for the function in class impl with the same name
void QuickLogger::Setbuffersize(unsigned int buffersize){
    this->PrivateImpl->Setbuffersize(buffersize);
}
//--------------------------------------------------------------------------
void QuickLogger::impl::Setbuffersize(unsigned int size){
    this->buffersize = size;
}
