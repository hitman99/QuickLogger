/* 
 * File:   QUICKLogger.h
 * Author: hitman
 *
 * Created on January 7, 2013, 11:31 AM
 */

#ifndef QUICKLOGGER_H
#define	QUICKLOGGER_H
#include <string>
#include <memory>
using namespace std;
/*
 To Do:
 implement log levels
 * 
 */
class QuickLogger {
public:
    /**
     * Constructor. Any ofstream exceptions are written to stderr.
     * @param path - Required, path to the file. Could be absolute or relative.
     * @param name - File name. Note that the complete file name will be in
     *   format QL_<name>_YYYYMMDDHHmmSS.log.csv
     * @param rolloverperiod - Definition how often to rollover files. 
     *   Several format options are available as below
     *   -> Time-frame definitions. Available definitions are: 
     *      <second>, <minute>, <hour>, <day>, <week>, <month>, <year> 
     *      A multiplier could be supplied to the time-frames state above,
     *      the format is <multiplier> <time-frame>[<@hh:mm:ss>]
     *      Here, multiplier can be any positive integer, time-frame could be 
     *      any of the time-frame definitions above. Also, additional hour,
     *      minute and second could be supplied to customize the rollover event.
     *      NOTE: hour, minute and second are only available to specify with the
     *      following time-frames: <day>,<week>,<month>,<year>, otherwise it
     *      will simply be ignored. If time parameter is not given with the
     *      time-frames which accept it, default rollover time will be 00:00:00.
     *      Examples: '200 seconds', '30 minutes', '1 hour', '1 day@15:00',
     *                '2 weeks@12:00:00' 
     *   -> Week days. Format is <weekday>[<@hh:mm:ss>]. Available weekdays are
     *      <Monday>,<Tuesday>,<Wednesday>,<Thursday>,<Friday>, <Saturday>,
     *      <Sunday>. It is also possible to precise the rollover event with
     *      optional time argument. Default value is 00:00:00
     *      Examples: 'Sunday@03:00:00', 'Monday', 'Friday@09'.
     *     
     *  If nothing is supplied or argument could not be parsed, rollover will
     *  occur every 86400 seconds, from the start of the application.                          
     */
    QuickLogger(string path, 
                string name,
                string rolloverperiod = "");
    /** 
     * QuickLogger object is not copyable, thus copy constructor is disabled. 
     */
    QuickLogger(const QuickLogger& orig) =delete;
    /**
     Destructor
     */
    ~QuickLogger();
    /**
     * Write message to the log file. Any exceptions are written to stderr.
     * Average latency of this call is around 10 microseconds on an average
     * end-user PC or laptop.
     * @param message
     * @param loglevel - a custom or standard level(if custom is not defined).
     *  Default log levels are: FATAL,ERROR,WARNING,INFO,DEBUG. Log levels are
     *  case sensitive.
     * @param component - Component name, could be omitted.
     */
    void Log(string message, string loglevel, string component = "");
    /**
     * Use this function to set the desirable order of fields-per-line.
     *  
     * @param fields - a list of fields separated by comma.
     *  Available fields are (case sensitive): TIME,LEVEL,COMPONENT,MESSAGE.
     *  At least 1 field should be set, otherwise, default fields will be used.
     */
    void Setfields(string fields);
    /**
     * Set log levels, all string values available. Use the same values when
     * logging any messages.
     * @param levels - a list of log levels separated by comma.
     *  Default fields are: FATAL,ERROR,WARNING,INFO,DEBUG
     */
    void Setloglevels(string levels);
    /**
     * Get the counter of buffer overflows for the current file. This counter
     * is reset for each file and printed on the last line of the file.
     * @return long - counter of buffer overflows.
     */
    long Getbufferoverflows();
    /**
     * Returns the currently opened absolute file name
     * @return string - filename
     */
    string Getfilename();
    /**
     * Toggle log level
     * @param level - string representation of the log level
     * @param enabled - trivial - true or false 
     */
    void Toggleloglevel(string level, bool enabled);
    /* With the configuration below it's possible to tune the performance of
     * the QuickLogger. With default configuration QuickLogger is able to handle
     * 35 000 MPS (messages per second) without buffer overflows on an average
     * end-user PC or laptop. */
    /**
     * Sets the flush frequency at which buffer is flushed to disk.
     * Default is 10 milliseconds
     * @param flushfreq - time, in milliseconds
     */
    void Setflushfrequency(unsigned int flushfreq);
    /**
     * Sets the buffer size. Small buffer will probably cause more buffer 
     * overflows
     * Default is 1000 messages.
     * @param buffersize - buffer size in messages
     */
    void Setbuffersize(unsigned int buffersize);
private:
    /**
     * Private implementation of the library. This approach allows updates of
     * the library (if interface does not change) without the recompilation of
     * user application.
     */
    class impl;
    std::unique_ptr<impl> PrivateImpl;
};

#endif	/* QUICKLOGGER_H */

