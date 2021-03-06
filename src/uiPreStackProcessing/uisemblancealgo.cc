/*+
 * (C) dGB Beheer B.V.; (LICENSE) http://opendtect.org/OpendTect_license.txt
 * AUTHOR   : K. Tingdahl
 * DATE     : Nov 2010
-*/


#include "uisemblancealgo.h"
#include "ptrman.h"

namespace PreStack
{

mImplClassFactory( uiSemblanceAlgorithm, factory );

uiSemblanceAlgorithm::uiSemblanceAlgorithm( uiParent* p, const HelpKey& helpkey)
    : uiDialog( p,
	  uiDialog::Setup(uiStrings::sSetup(),tr("Semblance parameters"),
                          helpkey).canceltext(uiString::empty()))
{}


}; //namespace
