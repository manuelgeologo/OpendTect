/*+
 * COPYRIGHT: (C) dGB Beheer B.V.
 * AUTHOR   : A.H. Bril
 * DATE     : Sep 2003
-*/

static const char* rcsID = "$Id: attribfactory.cc,v 1.1 2005-01-26 09:15:22 kristofer Exp $";

#include "attribfactory.h"

#include "attribdesc.h"
#include "attribparamset.h"
#include "msgh.h"

namespace Attrib
{

ProviderFactory::ProviderFactory()
{}


ProviderFactory::~ProviderFactory()
{
    for ( int idx=0; idx<paramsets.size(); idx++ )
	paramsets[idx]->unRef();

    paramsets.erase();
    creaters.erase();
}


void ProviderFactory::addParamSet( const ParamSet& nps, ProviderCreater pc )
{
    const int idx = indexOf(nps.name());
    if ( idx!=-1 )
    {
	UsrMsg( "Attribute name does allready exist in factory and is thus not" 
		" added", MsgClass::Error );
	return;
    }

    ParamSet* ps = nps.clone();
    ps->ref();

    paramsets += ps;
    creaters += pc;
};


Provider* ProviderFactory::create( Desc& desc ) const
{
    BufferString attribname = ParamSet::getAttribName( desc.defStr() );
    const int idx = indexOf(attribname);
    if ( idx==-1 ) return 0;

    ParamSet* ps = paramsets[idx]->clone();
    if ( !ps ) return 0;
    ps->ref();

    Provider* provider = 0;
    if ( ps->parse( desc.defStr() ) )
	provider = creaters[idx]( desc );
	
    ps->unRef();
    return provider;
}


int ProviderFactory::indexOf( const char* nm ) const
{
    for ( int idx=0; idx<paramsets.size(); idx++ )
    {
	if ( !strcmp( paramsets[idx]->name(), nm ) )
	    return idx;
    }

    return -1;
}

}; //namespace

