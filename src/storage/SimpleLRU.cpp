#include "SimpleLRU.h"

#include <iostream>

namespace Afina {
namespace Backend {

// See MapBasedGlobalLockImpl.h
bool SimpleLRU::Put(const std::string &key, const std::string &value)
{
    if (key.size() + value.size() > _max_size)
    {
        return false;
    }
    else
    {
        const lru_map::iterator iter = _lru_index.find(key);

        if (iter != _lru_index.end())//found in index
        {
            Update(iter,value);
        }
        else//not found in index
        {
            Insert(key,value);
        }
        return true;
    }
}

// See MapBasedGlobalLockImpl.h
bool SimpleLRU::PutIfAbsent(const std::string &key, const std::string &value)
{
    if ((key.size() + value.size() <= _max_size) && (_lru_index.find(key) == _lru_index.end()))
    {
        Insert(key,value);
        return true;
    }
    else
    {
        return false;
    }
}

// See MapBasedGlobalLockImpl.h
bool SimpleLRU::Set(const std::string &key, const std::string &value)
{
    const lru_map::iterator iter = _lru_index.find(key);
    if ((iter != _lru_index.end()) && (key.size() + value.size() <= _max_size))
    {
        Update(iter,value);
        return true;
    }
    else
    {
        return false;
    }
}

// See MapBasedGlobalLockImpl.h
bool SimpleLRU::Delete(const std::string &key)
{
    const lru_map::iterator iter = _lru_index.find(key);
    if (iter != _lru_index.end())
    {
        Remove(iter);
        return true;
    }
    else
    {
        return false;
    }
}

// See MapBasedGlobalLockImpl.h
bool SimpleLRU::Get(const std::string &key, std::string &value)
{
    const lru_map::iterator iter = _lru_index.find(key);
    if (iter != _lru_index.end())
    {
        value = iter->second.get().value;
        Rebase(iter,value);
        return true;
    }
    else
    {
        return false;
    }
}


//=========================================================================================================================\\


void SimpleLRU::Insert(const std::string& key, const std::string& value)
{

    while(_cur_size + value.size() + key.size()  > _max_size)
    {
        const lru_map::iterator rem_index_iter = _lru_index.find(_lru_head.get()->key);
        Remove(rem_index_iter);
    }

    _cur_size += key.size() + value.size();
    lru_node* _lru_new_node = new lru_node(key,value);

    //rebasing
    if (_lru_tail != nullptr)
    {
        _lru_new_node->prev = _lru_tail;
        _lru_new_node->next.reset();

        _lru_tail->next.reset(_lru_new_node);
        _lru_tail = _lru_tail->next.get();
    }
    else//first elem in storage
    {
        _lru_tail = _lru_new_node;
        _lru_head.reset(_lru_new_node);
    }

    // _lru_index insert new elem
    _lru_index.insert(std::make_pair(std::reference_wrapper<const std::string>(_lru_tail->key),std::reference_wrapper<lru_node>(*_lru_tail)));
}

void SimpleLRU::Update(const lru_map::iterator& upd_elem_iter, const std::string& new_value)
{
    while(_cur_size + (new_value.size() - upd_elem_iter->second.get().value.size())  > _max_size)
    {
        lru_map::iterator rem_index_iter = _lru_index.find(_lru_head.get()->key);
        if (upd_elem_iter != rem_index_iter)//skipping element which we want to update
        {
            Remove(rem_index_iter);
        }
        else
        {
            rem_index_iter = _lru_index.find(_lru_head->next.get()->key);
        }
    }

    //because of size_t
    _cur_size += new_value.size();
    _cur_size -= upd_elem_iter->second.get().value.size();

    //update tree node value, rebase node to tail
    Rebase(upd_elem_iter,new_value);

    //if tail, do nothing (also handles when _lru_tail == _lru_head)
}

void SimpleLRU::Rebase(const lru_map::iterator& push_map_iter,const std::string& new_value)
{
    lru_node& upd_node = push_map_iter->second.get();
    upd_node.value = new_value;

    if (&upd_node != _lru_tail)
    {
        if (upd_node.prev == nullptr)//is head
        {
            _lru_head.swap((&upd_node)->next);
            _lru_head->prev = nullptr;
        }
        else
        {
            upd_node.next->prev = upd_node.prev;
            upd_node.prev->next.swap(upd_node.next);
        }

        _lru_tail->next.swap(upd_node.next);
        upd_node.prev = _lru_tail;
        _lru_tail = (&upd_node);
    }
}

void SimpleLRU::Remove(const lru_map::iterator& rem_map_iter)
{
    lru_node& rem_node = rem_map_iter->second.get();//removing node reference
    _cur_size -= rem_node.key.size() + rem_node.value.size();
    const std::string& rem_key = rem_node.key;

    _lru_index.erase(rem_key);


    if (_lru_tail->prev == nullptr)//last elem,head == tail -> deleting it
    {
        _lru_tail = nullptr;
        _lru_head.reset();
    }
    else if (rem_key == _lru_head.get()->key)//head
    {
        _lru_head->next->prev = nullptr;
        lru_node* released_ptr = _lru_head->next.release();//must be put in mutex,can be done with swap maybe
        _lru_head.reset(released_ptr);
    }
    else if (rem_key == _lru_tail->key)//tail
    {
        _lru_tail = _lru_tail->prev;
        _lru_tail->next->prev = nullptr;
        _lru_tail->next.reset();
    }
    else
    {
        rem_node.next->prev = rem_node.prev;
        lru_node* released_ptr = rem_node.next.release();//must be put in mutex
        rem_node.prev->next.reset(released_ptr);
    }
}

} // namespace Backend
} // namespace Afina
