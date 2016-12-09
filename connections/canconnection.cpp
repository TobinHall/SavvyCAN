#include <QThread>
#include "canconnection.h"


struct BusData {
    CANBus             mBus;
    bool               mConfigured;
    QVector<CANFlt>    mFilters;
    bool               mFilterOut;
};


CANConnection::CANConnection(QString pPort,
                             CANCon::type pType,
                             int pNumBuses,
                             int pQueueLen,
                             bool pUseThread) :
    mQueue(),
    mNumBuses(pNumBuses),
    mPort(pPort),
    mType(pType),
    mIsCapSuspended(false),
    mStatus(CANCon::NOT_CONNECTED),
    mStarted(false),
    mThread_p(NULL)
{
    /* register types */
    qRegisterMetaType<CANBus>("CANBus");
    qRegisterMetaType<CANFrame>("CANFrame");
    qRegisterMetaType<CANCon::status>("CANCon::status");
    qRegisterMetaType<CANFlt>("CANFlt");

    /* set queue size */
    mQueue.setSize(pQueueLen); /*TODO add check on returned value */

    /* allocate buses */
    /* TODO: change those tables for a vector */
    mBusData_p = new BusData[mNumBuses];
    for(int i=0 ; i<mNumBuses ; i++) {
        mBusData_p[i].mConfigured  = false;
        mBusData_p[i].mFilterOut   = false;
    }

    /* if needed, create a thread and move ourself into it */
    if(pUseThread) {
        mThread_p = new QThread();
    }
}


CANConnection::~CANConnection()
{
    /* stop and delete thread */
    if(mThread_p) {
        mThread_p->quit();
        mThread_p->wait();
        delete mThread_p;
        mThread_p = NULL;
    }

    if(mBusData_p) {
        delete[] mBusData_p;
        mBusData_p = NULL;
    }
}


void CANConnection::start()
{
    if( mThread_p && (mThread_p != QThread::currentThread()) )
    {
        /* move ourself to the thread */
        moveToThread(mThread_p); /*TODO handle errors */
        /* connect started() */
        connect(mThread_p, SIGNAL(started()), this, SLOT(start()));
        /* start the thread */
        mThread_p->start(QThread::HighPriority);
        return;
    }

    /* set started flag */
    mStarted = true;

    /* in multithread case, this will be called before entering thread event loop */
    return piStarted();
}


void CANConnection::suspend(bool pSuspend)
{
    /* execute in mThread_p context */
    if( mThread_p && (mThread_p != QThread::currentThread()) ) {
        QMetaObject::invokeMethod(this, "suspend",
                                  Qt::BlockingQueuedConnection,
                                  Q_ARG(bool, pSuspend));
        return;
    }

    return piSuspend(pSuspend);
}


void CANConnection::stop()
{
    /* 1) execute in mThread_p context */
    if( mThread_p && mStarted && (mThread_p != QThread::currentThread()) )
    {
        /* if thread is finished, it means we call this function for the second time so we can leave */
        if( !mThread_p->isFinished() )
        {
            /* we need to call piStop() */
            QMetaObject::invokeMethod(this, "stop",
                                      Qt::BlockingQueuedConnection);
            /* 3) stop thread */
            mThread_p->quit();
            if(!mThread_p->wait()) {
                qDebug() << "can't stop thread";
            }
        }
        return;
    }

    /* 2) call piStop in mThread context */
    return piStop();
}


bool CANConnection::getBusSettings(int pBusIdx, CANBus& pBus)
{
    /* make sure we execute in mThread context */
    if( mThread_p && (mThread_p != QThread::currentThread()) ) {
        bool ret;
        QMetaObject::invokeMethod(this, "getBusSettings",
                                  Qt::BlockingQueuedConnection,
                                  Q_RETURN_ARG(bool, ret),
                                  Q_ARG(int , pBusIdx),
                                  Q_ARG(CANBus& , pBus));
        return ret;
    }

    return piGetBusSettings(pBusIdx, pBus);
}


void CANConnection::setBusSettings(int pBusIdx, CANBus pBus)
{
    /* make sure we execute in mThread context */
    if( mThread_p && (mThread_p != QThread::currentThread()) ) {
        QMetaObject::invokeMethod(this, "setBusSettings",
                                  Qt::BlockingQueuedConnection,
                                  Q_ARG(int, pBusIdx),
                                  Q_ARG(CANBus, pBus));
        return;
    }

    return piSetBusSettings(pBusIdx, pBus);
}


bool CANConnection::sendFrame(const CANFrame& pFrame)
{
    /* make sure we execute in mThread context */
    if( mThread_p && (mThread_p != QThread::currentThread()) )
    {
        bool ret;
        QMetaObject::invokeMethod(this, "sendFrame",
                                  Qt::BlockingQueuedConnection,
                                  Q_RETURN_ARG(bool, ret),
                                  Q_ARG(const CANFrame&, pFrame));
        return ret;
    }

    return piSendFrame(pFrame);
}


bool CANConnection::sendFrames(const QList<CANFrame>& pFrames)
{
    /* make sure we execute in mThread context */
    if( mThread_p && (mThread_p != QThread::currentThread()) )
    {
        bool ret;
        QMetaObject::invokeMethod(this, "sendFrames",
                                  Qt::BlockingQueuedConnection,
                                  Q_RETURN_ARG(bool, ret),
                                  Q_ARG(const QList<CANFrame>&, pFrames));
        return ret;
    }

    return piSendFrames(pFrames);
}


int CANConnection::getNumBuses() const{
    return mNumBuses;
}


bool CANConnection::isConfigured(int pBusId) {
    if( pBusId < 0 || pBusId >= getNumBuses())
        return false;
    return mBusData_p[pBusId].mConfigured;
}

void CANConnection::setConfigured(int pBusId, bool pConfigured) {
    if( pBusId < 0 || pBusId >= getNumBuses())
        return;
    mBusData_p[pBusId].mConfigured = pConfigured;
}


bool CANConnection::getBusConfig(int pBusId, CANBus& pBus) {
    if( pBusId < 0 || pBusId >= getNumBuses() || !isConfigured(pBusId))
        return false;

    pBus = mBusData_p[pBusId].mBus;
    return true;
}


void CANConnection::setBusConfig(int pBusId, CANBus& pBus) {
    if( pBusId < 0 || pBusId >= getNumBuses())
        return;

    mBusData_p[pBusId].mConfigured = true;
    mBusData_p[pBusId].mBus = pBus;
}


QString CANConnection::getPort() {
    return mPort;
}


LFQueue<CANFrame>& CANConnection::getQueue() {
    return mQueue;
}


CANCon::type CANConnection::getType() {
    return mType;
}


CANCon::status CANConnection::getStatus() {
    return (CANCon::status) mStatus.load();
}

void CANConnection::setStatus(CANCon::status pStatus) {
    mStatus.store(pStatus);
}

bool CANConnection::isCapSuspended() {
    return mIsCapSuspended;
}

void CANConnection::setCapSuspended(bool pIsSuspended) {
    mIsCapSuspended = pIsSuspended;
}

bool CANConnection::setFilters(int pBusId, const QVector<CANFlt>& pFilters, bool pFilterOut)
{
    /* make sure we execute in mThread context */
    if( mThread_p && (mThread_p != QThread::currentThread()) ) {
        bool ret;
        QMetaObject::invokeMethod(this, "setFilters",
                                  Qt::BlockingQueuedConnection,
                                  Q_RETURN_ARG(bool, ret),
                                  Q_ARG(int , pBusId),
                                  Q_ARG(const QVector<CANFlt>&, pFilters),
                                  Q_ARG(bool , pFilterOut));
        return ret;
    }

    /* sanity checks */
    if(pBusId<0 || pBusId>=getNumBuses())
        return false;

    /* copy filters */
    mBusData_p[pBusId].mFilters    = pFilters;
    mBusData_p[pBusId].mFilterOut  = pFilterOut;

    /* set hardware filtering if available */
    if(pFilterOut)
        piSetFilters(pBusId, pFilters);

    return true;
}


bool CANConnection::discard(int pBusId, quint32 pId, bool& pNotify)
{
    if(pBusId<0 || pBusId>=getNumBuses())
        return true;

    foreach (const CANFlt& filter, mBusData_p[pBusId].mFilters)
    {
        if( (filter.id & filter.mask) == (pId & filter.mask) )
        {
            if(filter.notify)
                pNotify = true;
            return false;
        }
    }
    return mBusData_p[pBusId].mFilterOut;
}


bool CANConnection::piSendFrames(const QList<CANFrame>& pFrames)
{
    foreach(const CANFrame& frame, pFrames)
    {
        if(!piSendFrame(frame))
            return false;
    }

    return true;
}


/* default implementation of piSetFilters */
void CANConnection::piSetFilters(int pBusId, const QVector<CANFlt>& pFilters)
{
    Q_UNUSED(pBusId);
    Q_UNUSED(pFilters);
}