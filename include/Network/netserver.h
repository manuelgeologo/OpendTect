#pragma once

/*+
________________________________________________________________________

 (C) dGB Beheer B.V.; (LICENSE) http://opendtect.org/OpendTect_license.txt
 Author:	Nanne Hemstra
 Date:		March 2009
________________________________________________________________________

-*/


#include "networkcommon.h"

mFDQtclass(QTcpSocket)
mFDQtclass(QTcpServer)
mFDQtclass(QTcpServerComm)


namespace Network
{

class Socket;

mExpClass(Network) Server : public CallBacker
{
public:
			Server();
			~Server();

    bool		listen(SpecAddr=Any,PortNr_Type port=0);
			//!<If Any, server will listen to all network interfaces
    bool		isListening() const;
    PortNr_Type		port() const;
    Authority		authority() const;

    void		close();
    bool		hasPendingConnections() const;
    int			write(int id,const char*);
			//!<Writes to next pending socket
    int			write(int id,const IOPar&);
    void		read(int id,BufferString&) const;
    void		read(int id,IOPar&) const;
    const char*		errorMsg() const;

    Socket*		getSocket(int id);
    const Socket*	getSocket(int id) const;

    CNotifier<Server,int> newConnection;
    CNotifier<Server,int> readyRead;

    mQtclass(QTcpSocket)* nextPendingConnection();
			//!<Use when you want to access Qt object directly

    bool		waitForNewConnection(int msec);
			//!<Useful when no event loop available
			//!<If msec is -1, this function will not time out

    static const char*	sKeyHostName()		{ return "hostname"; }
    static const char*	sKeyNoListen()		{ return "no-listen"; }
    static const char*	sKeyPort()		{ return "port"; }
    static const char*	sKeyTimeout()		{ return "timeout"; }
    static const char*	sKeyKillword()		{ return "kill"; }

protected:

    void		notifyNewConnection();
    void		readyReadCB(CallBacker*);
    void		disconnectCB(CallBacker*);

    mQtclass(QTcpServer)* qtcpserver_;
    mQtclass(QTcpServerComm)* comm_;
    mutable BufferString errmsg_;
    ObjectSet<Socket>	sockets_;
    TypeSet<int>	ids_;
    ObjectSet<Socket>	sockets2bdeleted_;

    friend class	mQtclass(QTcpServerComm);

};

} // namespace Network
