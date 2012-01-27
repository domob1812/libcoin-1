
#ifndef TRANSACTIONFILTER_H
#define TRANSACTIONFILTER_H

#include "btcNode/PeerManager.h"
#include "btcNode/Filter.h"

#include "btc/serialize.h" // for CDataStream
#include "btc/util.h" // for CCriticalSection definition

#include <string>
#include <map>
#include <deque>

class BlockChain;
class Inventory;
class CDataStream;

class TransactionFilter : public Filter
{
public:

    TransactionFilter(BlockChain& bc) : _blockChain(bc) {}
    
    class Listener : private boost::noncopyable {
    public:
        virtual void operator()(const Transaction&) = 0;
    };
    typedef boost::shared_ptr<Listener> listener_ptr;
    typedef std::set<listener_ptr> Listeners;
    
    void subscribe(listener_ptr listener) { _listeners.insert(listener); }

    class Reminder : private boost::noncopyable {
    public:
        virtual void operator()(std::set<uint256>&) = 0;
    };
    typedef boost::shared_ptr<Reminder> reminder_ptr;
    typedef std::set<reminder_ptr> Reminders;

    void subscribe(reminder_ptr reminder) { _reminders.insert(reminder); }
        
    virtual bool operator()(Peer* origin, Message& msg);
    
    virtual std::set<std::string> commands() {
        std::set<std::string> c; 
        c.insert("tx");
        c.insert("inv");
        c.insert("getdata");
        return c;
    }
    
    /// Call process to get a hook into the transaction processing, e.g. if for injecting wallet generated txes
    void process(Transaction& tx, Peers peers);
    
private:
    BlockChain& _blockChain;
    Listeners _listeners;
    Reminders _reminders;

    std::map<uint256, CDataStream*> _orphanTransactions;
    std::multimap<uint256, CDataStream*> _orphanTransactionsByPrev;
    
    void addOrphanTx(const Transaction& tx);
    
    void eraseOrphanTx(uint256 hash);
    
    bool alreadyHave(const Inventory& inv);
    
    std::map<Inventory, int64> _alreadyAskedFor;
    
    /// The Relay system is only used for Transactions - hence we put it here.
    
    std::map<Inventory, CDataStream> _relay;
    std::deque<std::pair<int64, Inventory> > _relayExpiration;
    
    inline void relayInventory(const Peers& peers, const Inventory& inv);

    template<typename T> void relayMessage(const Peers& peers, const Inventory& inv, const T& a);    
};

template<> inline void TransactionFilter::relayMessage<>(const Peers& peers, const Inventory& inv, const CDataStream& ss);


#endif // TRANSACTIONFILTER_H