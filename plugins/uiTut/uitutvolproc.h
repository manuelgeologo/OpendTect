#ifndef uitutvolproc_h
#define uitutvolproc_h

/*+
________________________________________________________________________

 (C) dGB Beheer B.V.; (LICENSE) http://opendtect.org/OpendTect_license.txt
 Author:	H. Huck
 Date:		March 2016
 RCS:		$Id$
________________________________________________________________________

-*/

#include "uitutmod.h"
#include "uivolprocchain.h"
#include "tutvolproc.h"

class uiGenInput;
class uiStepOutSel;

namespace VolProc
{

class TutOpCalculator;


mExpClass(uiTut) uiTutOpCalculator : public uiStepDialog
{ mODTextTranslationClass(uiTutOpCalculator);
public:
    mDefaultFactoryInstantiationBase(
	VolProc::TutOpCalculator::sFactoryKeyword(),
	VolProc::TutOpCalculator::sFactoryDisplayName())
	mDefaultFactoryInitClassImpl( uiStepDialog, createInstance );


protected:

				uiTutOpCalculator(uiParent*,TutOpCalculator*,
						  bool is2d);
    static uiStepDialog*	createInstance(uiParent*,Step*,bool is2d);

    bool			acceptOK();
    void			typeSel(CallBacker*);

    TutOpCalculator*		opcalc_;

    uiGenInput*			typefld_;
    uiStepOutSel*		shiftfld_;
};

} // namespace VolProc

#endif
