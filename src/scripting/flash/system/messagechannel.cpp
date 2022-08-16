/**************************************************************************
    Lightspark, a free flash player implementation

    Copyright (C) 2009-2013  Alessandro Pignotti (a.pignotti@sssup.it)

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU Lesser General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU Lesser General Public License for more details.

    You should have received a copy of the GNU Lesser General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
**************************************************************************/

#include "scripting/abc.h"
#include "scripting/flash/system/messagechannel.h"
#include "scripting/flash/concurrent/Condition.h"
#include "scripting/flash/errors/flasherrors.h"
#include "scripting/flash/system/flashsystem.h"
#include "scripting/class.h"
#include "scripting/argconv.h"

using namespace lightspark;

void MessageChannel::sinit(Class_base* c)
{
	CLASS_SETUP_NO_CONSTRUCTOR(c, EventDispatcher, CLASS_SEALED|CLASS_FINAL);
	c->isReusable=true;
	REGISTER_GETTER(c, state);
	c->setDeclaredMethodByQName("messageAvailable","",Class<IFunction>::getFunction(c->getSystemState(),messageAvailable,0,Class<Boolean>::getRef(c->getSystemState()).getPtr()),GETTER_METHOD,true);
	c->setDeclaredMethodByQName("addEventListener","",Class<IFunction>::getFunction(c->getSystemState(),_addEventListener),NORMAL_METHOD,true);
	c->setDeclaredMethodByQName("removeEventListener","",Class<IFunction>::getFunction(c->getSystemState(),_removeEventListener),NORMAL_METHOD,true);
	c->setDeclaredMethodByQName("close","",Class<IFunction>::getFunction(c->getSystemState(),close),NORMAL_METHOD,true);
	c->setDeclaredMethodByQName("receive","",Class<IFunction>::getFunction(c->getSystemState(),receive,0,Class<ASObject>::getRef(c->getSystemState()).getPtr()),NORMAL_METHOD,true);
	c->setDeclaredMethodByQName("send","",Class<IFunction>::getFunction(c->getSystemState(),send),NORMAL_METHOD,true);
	c->setDeclaredMethodByQName("toString","",Class<IFunction>::getFunction(c->getSystemState(),_toString,0,Class<ASString>::getRef(c->getSystemState()).getPtr()),NORMAL_METHOD,true);
}

void MessageChannel::finalize()
{
	{
		Locker l(messagequeuemutex);
		auto it = messagequeue.begin();
		while (it != messagequeue.end())
		{
			(*it)->removeStoredMember();
			it = messagequeue.erase(it);
		}
	}
	if (sender)
		sender->removeStoredMember();
	sender=nullptr;
	if (receiver)
		receiver->removeStoredMember();
	receiver=nullptr;
}
bool MessageChannel::destruct()
{
	{
		Locker l(messagequeuemutex);
		auto it = messagequeue.begin();
		while (it != messagequeue.end())
		{
			(*it)->removeStoredMember();
			it = messagequeue.erase(it);
		}
	}
	if (sender)
		sender->removeStoredMember();
	sender=nullptr;
	if (receiver)
		receiver->removeStoredMember();
	receiver=nullptr;
	return EventDispatcher::destruct();
}
void MessageChannel::prepareShutdown()
{
	if (this->preparedforshutdown)
		return;
	EventDispatcher::prepareShutdown();
	{
		Locker l(messagequeuemutex);
		for (auto it = messagequeue.begin(); it != messagequeue.end(); it++)
			(*it)->prepareShutdown();
	}
	if (sender)
		sender->prepareShutdown();
	if (receiver)
		receiver->prepareShutdown();
}
uint32_t MessageChannel::countCylicMemberReferences(ASObject* obj, uint32_t needed, bool firstcall)
{
	if (obj==this && !firstcall)
		return 1;
	uint32_t res=0;
	{
		Locker l(messagequeuemutex);
		for (auto it = messagequeue.begin(); it != messagequeue.end(); it++)
		{
			if (res>needed)
				return res;
			ASObject* o = (*it);
			if (o == obj)
				++res;
			if (!o->getConstant() && o->isLastRef() && o->canHaveCyclicMemberReference())
			{
				uint32_t r = o->countCylicMemberReferences(obj,needed-res,false);
				if (r == UINT32_MAX)
					return UINT32_MAX;
				res += r;
			}
		}
	}
	if (sender)
	{
		if (sender == obj)
			++res;
		if (!sender->getConstant() && sender->isLastRef() && sender->canHaveCyclicMemberReference())
		{
			uint32_t r = sender->countCylicMemberReferences(obj,needed-res,false);
			if (r == UINT32_MAX)
				return UINT32_MAX;
			res += r;
		}
	}
	if (receiver)
	{
		if (receiver == obj)
			++res;
		if (!receiver->getConstant() && receiver->isLastRef() && receiver->canHaveCyclicMemberReference())
		{
			uint32_t r = receiver->countCylicMemberReferences(obj,needed-res,false);
			if (r == UINT32_MAX)
				return UINT32_MAX;
			res += r;
		}
	}
	uint32_t r = EventDispatcher::countCylicMemberReferences(obj,needed-res,firstcall);
	if (r == UINT32_MAX)
		return UINT32_MAX;
	res += r;
	return res;
}

ASFUNCTIONBODY_GETTER(MessageChannel, state)

ASFUNCTIONBODY_ATOM(MessageChannel,messageAvailable)
{
	MessageChannel* th=asAtomHandler::as<MessageChannel>(obj);
	Locker l(th->messagequeuemutex);
	ret = asAtomHandler::fromBool(!th->messagequeue.empty());
}

ASFUNCTIONBODY_ATOM(MessageChannel,_addEventListener)
{
	MessageChannel* th=asAtomHandler::as<MessageChannel>(obj);
	if (argslen >=2 && asAtomHandler::isFunction(args[1]))
	{
		// the function will be executed in the receiver worker, so set its worker accordingly
		asAtomHandler::as<IFunction>(args[1])->setWorker(th->receiver);
		asAtomHandler::as<IFunction>(args[1])->objfreelist=nullptr;
		
	}
	EventDispatcher::addEventListener(ret,wrk,obj,args,argslen);
}
ASFUNCTIONBODY_ATOM(MessageChannel,_removeEventListener)
{
	EventDispatcher::removeEventListener(ret,wrk,obj,args,argslen);
}
ASFUNCTIONBODY_ATOM(MessageChannel,_toString)
{
	EventDispatcher::_toString(ret,wrk,obj,args,argslen);
}
ASFUNCTIONBODY_ATOM(MessageChannel,close)
{
	MessageChannel* th=asAtomHandler::as<MessageChannel>(obj);
	if (th->state == "open")
		th->state="closing";
}
ASFUNCTIONBODY_ATOM(MessageChannel,receive)
{
	MessageChannel* th=asAtomHandler::as<MessageChannel>(obj);
	bool blockUntilReceived;
	ARG_UNPACK_ATOM(blockUntilReceived,false);
	Locker l(th->messagequeuemutex);
	if (th->messagequeue.empty())
	{
		if (blockUntilReceived)
		{
			while (th->messagequeue.empty() && th->state=="open")
			{
				l.release();
				compat_msleep(100);
				l.acquire();
			}
		}
		if (th->messagequeue.empty())
		{
			ret = asAtomHandler::nullAtom;
			return;
		}
	}
	
	ASObject* msg = th->messagequeue.front();
	th->messagequeue.pop_front();
	if (msg->is<ASWorker>()
			|| msg->is<MessageChannel>()
			|| (msg->is<ByteArray>() && msg->as<ByteArray>()->shareable)
			|| msg->is<ASMutex>()
			|| msg->is<ASCondition>()
			)
	{
		msg->incRef();
		msg->removeStoredMember();
		ret = asAtomHandler::fromObjectNoPrimitive(msg);
	}
	else
	{
		ret = msg->as<ByteArray>()->readObject();
		msg->removeStoredMember();
	}
}
ASFUNCTIONBODY_ATOM(MessageChannel,send)
{
	MessageChannel* th=asAtomHandler::as<MessageChannel>(obj);
	if (th->state!= "open")
		throw Class<IOError>::getInstanceS(wrk,"MessageChannel closed");
	_NR<ASObject> msg;
	int queueLimit;
	ARG_UNPACK_ATOM(msg)(queueLimit,-1);
	if (msg.isNull())
		return;
	if (queueLimit != -1)
		LOG(LOG_NOT_IMPLEMENTED,"MessageChannel.send ignores parameter queueLimit");
	Locker l(th->messagequeuemutex);
	if (msg->is<ASWorker>()
			|| msg->is<MessageChannel>()
			|| (msg->is<ByteArray>() && msg->as<ByteArray>()->shareable)
			|| msg->is<ASMutex>()
			|| msg->is<ASCondition>()
			)
	{
		msg->objfreelist=nullptr; // message will be used in another thread, make it not reusable
		msg->incRef();
		msg->addStoredMember();
		th->messagequeue.push_back(msg.getPtr());
	}
	else
	{
		ByteArray* b = Class<ByteArray>::getInstanceSNoArgs(th->receiver);
		b->writeObject(msg.getPtr(),th->receiver);
		b->setPosition(0);
		b->addStoredMember();
		th->messagequeue.push_back(b);
	}
	th->incRef();
	getVm(wrk->getSystemState())->addEvent(_MR(th),_MR(Class<Event>::getInstanceS(th->receiver,"channelMessage")));
}
