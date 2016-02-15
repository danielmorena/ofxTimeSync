
//----------------------------------------------------------------------------//
#ifndef DEX_PHASE_LOCKED_CLOCK_CPP
#define DEX_PHASE_LOCKED_CLOCK_CPP
//----------------------------------------------------------------------------//
#include "PhaseLockedClock.h"
#include "Exceptions.h"
#include <iostream>
//----------------------------------------------------------------------------//
using namespace std;
namespace dex {
//----------------------------------------------------------------------------//

PhaseLockedClock::PhaseLockedClock(Clock& primary, Clock& reference)
    : primaryClock_(primary),
      referenceClock_(reference),
      variableFrequencyClock_(primary),
      updateInterval_(1000000),
      inSync_(false),
      primaryFrequencyAvg_(1000000),
      phasePanic_(5000),
      updatePanic_(5000000),
      lastUpdate_(0)
{
	this->setPriority(Thread::PRIO_HIGH);
    start(*this);
}


PhaseLockedClock::~PhaseLockedClock()
{
    terminate();
}


timestamp_t PhaseLockedClock::getValue()
{
    if (!inSync_) throw ClockException("PhaseLockedClock not in sync");
    
    try
    {
        enterMutex();
        timestamp_t time = variableFrequencyClock_.getValue();
        leaveMutex();
        return time;
    }
    catch (ClockException e)
    {
        leaveMutex();
        //cout << "PhaseLockedClock picked up exception, now out of sync." << endl;
        inSync_ = false;
        throw e;
    }
    
}


bool PhaseLockedClock::isSynchronized()
{
    return inSync_;
}

int PhaseLockedClock::getOffset()
{
    if (!inSync_) throw ClockException("PhaseLockedClock not in sync");
    enterMutex();
    int offset = (int) thisPhase_;
    leaveMutex();
    return offset;
}


void PhaseLockedClock::setPhasePanic(timestamp_t phasePanic)
{
    phasePanic_ = phasePanic;
}
    
void PhaseLockedClock::setUpdatePanic(timestamp_t updatePanic)
{
    updatePanic_ = updatePanic;
}

void PhaseLockedClock::run()
{
    //cout << "thread running" << endl;
    
    while (isRunning())
    {
        update();

        // todo:
        // put in a timestamp for the last sucessfull update
        // if it has been a while, we declare this clock out of sync.
    
        double variance = updateInterval_ / 10.0;
        double base = updateInterval_ - (variance / 2.0);
        double random = ( rand() % (int)(variance * 1000) ) / 1000.0;
        int sleep_ms = (int) ( (base + random) / 1000 );
        
        sleep(sleep_ms);
    }
    
    // exit();
}

void PhaseLockedClock::update()
{


    if (inSync_ && (primaryClock_.getValue() - lastUpdate_) > updatePanic_)
    {
        //cout << "last update too long ago." << endl;
        inSync_ = false;
    }
    
    if (!inSync_)
    {
        //cout << "CLOCK OUT OF SYNC" << endl;
        setClock();
        if (inSync_)
        {
            if ( updatePhase() )
                lastUpdate_ = primaryClock_.getValue();
        }
        return;
    }
    
    bool phaseUpdated = updatePhase();
    if (!phaseUpdated)
    {
        //cout << "PHASE UPDATE FAILED" << endl;
        return;
    }
    
    bool clockUpdated = updateClock();
    if (!clockUpdated)
    {
        //cout << "CLOCK UPDATE FAILED" << endl;
        return;
    }
    
    // mark a timestamp for sucessfull update.
    lastUpdate_ = primaryClock_.getValue();
}


bool PhaseLockedClock::updatePhase()
{
    try
    {
        enterMutex();
        
        timestamp_t phase = referenceClock_.getPhase(variableFrequencyClock_);
        timestamp_t variableValue = variableFrequencyClock_.getValue();
        timestamp_t primaryValue = primaryClock_.getValue();

        leaveMutex();
        
        lastPhase_ = thisPhase_;
        lastVariableValue_ = thisVariableValue_;
        lastPrimaryValue_ = thisPrimaryValue_;
        
        thisPhase_ = phase;
        thisVariableValue_ = variableValue;
        thisPrimaryValue_ = primaryValue;
        
        //cout << "detected phase: " << ((int)phase) << endl;
        
        return true;
    }
    catch (ClockException e)
    {
        leaveMutex();
        //cout << "PLC handling clock exception: " << e.getMessage() << endl;
        return false;
    }
}


bool PhaseLockedClock::updateClock()
{

	// calculate the elapsed time in seconds on the reference clock
    timestamp_t lastReferenceValue = lastVariableValue_ + lastPhase_;
    timestamp_t thisReferenceValue = thisVariableValue_ + thisPhase_;
    const double referenceElapsed = thisReferenceValue - lastReferenceValue;
        
	// find the primary clock's frequency (filter for noise)
    const double primaryTicks = thisPrimaryValue_ - lastPrimaryValue_;
    const double primaryFrequency = 1e6 * primaryTicks / referenceElapsed;
    primaryFrequencyAvg_ += (primaryFrequency - primaryFrequencyAvg_) * 0.1;

    // cout << "primary clock frequency average: " << ((int)primaryFrequencyAvg_) << endl;

    // if the phase is too high, we declare the clock out of sync
    if ( ( thisPhase_ > phasePanic_ ) || (thisPhase_ < (-1 * phasePanic_)))
    {
        //cout << "phase too high" << endl;
        inSync_ = false;
        return false;
    }

	// calculate the adjustment for the variable clock's frequency
    const double phaseDiff = thisPhase_ * 0.1;
    const double frequencyDiff = 1000000 - primaryFrequencyAvg_;
	const double variableClockFrequency = 1000000 + (frequencyDiff + phaseDiff);

    // cout << "using frequency: " << ((int) variableClockFrequency) << endl;

    enterMutex();
	variableFrequencyClock_.setFrequency( (int) variableClockFrequency );
    leaveMutex();

    return true;
}

void PhaseLockedClock::setClock()
{
    
    enterMutex();
    
    try
    {
        variableFrequencyClock_.setValue(referenceClock_.getValue());
        inSync_ = true;
        //cout << "CLOCK IN SYNC" << endl;
    }
    catch (ClockException e)
    {
        //cout << "exception while resetting to the reference clock" << endl;
        inSync_ = false;
    }
    
    leaveMutex();
    
}

void PhaseLockedClock::enterMutex()
{
	mutex_.lock();
}

void PhaseLockedClock::leaveMutex()
{
	mutex_.unlock();
}

//----------------------------------------------------------------------------//
} // namespace dex
//----------------------------------------------------------------------------//
#endif //DEX_PHASE_LOCKED_CLOCK_CPP
//----------------------------------------------------------------------------//

