/*+
 * (C) dGB Beheer B.V.; (LICENSE) http://opendtect.org/OpendTect_license.txt
 * AUTHOR   : K. Tingdahl
 * DATE     : Jan 2014
 * FUNCTION :
-*/


#include "uistring.h"
#include "testprog.h"
#include "uistrings.h"
#include "texttranslator.h"
#include "filepath.h"
#include "oddirs.h"

#include <QString>
#include <QByteArray>
#include <QTranslator>


bool testSetEmpty()
{
    uiString str = toUiString("Hello");
    str.setEmpty();
    mRunStandardTest( str.isEmpty(), "isEmpty" );

    str.setFrom( QString("Hello" ) );
    str.setEmpty();
    mRunStandardTest( str.isEmpty(), "isEmpty after setFrom(QString)" );

    return true;
}

class TestTranslator
{ mTextTranslationClass( TestTranslator, "test_uistring" );
public:
    static bool testTranslation();

};


bool TestTranslator::testTranslation()
{
    uiString a = tr("I am an A");
    uiString b = tr("I am a B" );
    uiString join = uiStrings::phrJoinStrings( a, b );

    QTranslator trans;
    File::Path path;
    TextTranslateMgr::GetLocalizationDir( path );
    path.add( "uistring.qm" );
    mRunStandardTest( trans.load( QString(path.fullPath().buf())),
		    "Load test translation");

    QString qres;
    mRunStandardTest( join.translate( trans, qres ),
		      "Run translation" );

    BufferString res( qres );
    mRunStandardTest( res=="A B", "Translation content");

    uiString hor3d =
	uiStrings::phrJoinStrings(uiStrings::s3D(), uiStrings::sHorizon() );

    qres = hor3d.getQString();
    res = qres;
    mRunStandardTest( res=="3D Horizon", "Translation content (Horizon)");

    hor3d =
     uiStrings::phrJoinStrings(uiStrings::s3D(), uiStrings::sHorizon(mPlural) );

    qres = hor3d.getQString();
    res = qres;
    mRunStandardTest( res=="3D Horizons", "Translation content (Horizons)");

    return true;
}



bool testArg()
{
    uiString composite = toUiString( "%1 plus %2 is %3")
		.arg( 4 )
		.arg( 5 )
		.arg( 9 );

    mRunStandardTest( composite.getFullString()=="4 plus 5 is 9",
		      "Composite test" );

    const char* desoutput = "Hello Dear 1";

    uiString string = toUiString( "Hello %1 %2").arg( "Dear" ).arg(toString(1));
    mRunStandardTest( string.getQString()==QString( desoutput ),
		     "Standard argument order");

    string = toUiString( "Hello %2 %1").arg( toString( 1 ) ).arg( "Dear" );
    mRunStandardTest( string.getQString()==QString(desoutput),
		     "Reversed argument order");

    string = toUiString( "Hello %1 %2");
    string.arg( "Dear" ).arg( toString(1) );
    mRunStandardTest( string.getQString()==QString(desoutput),
		     "In-place");


    BufferString expargs = string.getFullString();

    mRunStandardTest( expargs==desoutput, "Argument expansion" );

    uiString cloned;
    cloned = string;
    cloned.makeIndependent();

    mRunStandardTest( string.getQString()==cloned.getQString(), "copyFrom" );

    const uiString part1 = toUiString( "Part 1" );
    const uiString part2 = toUiString( "Part 2" );
    uiString res = part1; res.appendPhrase( part2, uiString::BluntGlue );
    mRunStandardTest( res.isEqualTo(toUiString("Part 1Part 2")),
				"appendPhrase(BluntGlue)" )
    res = part1; res.appendPhrase( part2, uiString::WithSpace );
    mRunStandardTest( res.isEqualTo(toUiString("Part 1 Part 2")),
				"appendPhrase(WithSpace)" )
    res = part1; res.appendPhrase( part2, uiString::NewLine );
    mRunStandardTest( res.isEqualTo(toUiString("Part 1\nPart 2")),
				"appendPhrase(NewLine)" )
    res = part1; res.appendPhrase( part2, uiString::CloseLine );
    mRunStandardTest( res.isEqualTo(toUiString("Part 1. Part 2")),
				"appendPhrase(CloseLine)" )
    res = part1; res.appendPhrase( part2, uiString::CloseAndNewLine );
    mRunStandardTest( res.isEqualTo(toUiString("Part 1.\nPart 2")),
				"appendPhrase(CloseAndNewLine)" )

    return true;
}


bool testSharedData()
{
    uiString a = toUiString("Hello %1%2").arg( "World" );
    uiString b = a;

    b.arg( "s" );
    mRunStandardTest( b.getFullString()=="Hello Worlds" &&
		      BufferString(a.getFullString())!=
		      BufferString(b.getFullString()), "arg on copy" );

    uiString c = b;
    c = toUiString("Another message");
    mRunStandardTest( BufferString(c.getFullString())!=
		      BufferString(b.getFullString()),
		      "assignment of copy" );

    uiString d = b;
    mRunStandardTest( d.getOriginalString()==b.getOriginalString(),
		      "Use of same buffer on normal operations" );

    return true;
}


bool testIsEqual()
{
    const uiString a = toUiString( "A" );
    const uiString a2 = toUiString( "A" );
    const uiString b = toUiString( "B" );

    mRunStandardTest( a.isEqualTo(a2),
	    "Is equal is true for equal strings, test 1" );
    mRunStandardTest( !a.isEqualTo(b),
	    "Is equal is false for different strings, test 1" );

    mRunStandardTest( a2.isEqualTo(a),
	    "Is equal is true for equal strings, test 2" );
    mRunStandardTest( !b.isEqualTo(a),
	    "Is equal is false for different strings, test 2" );

    return true;
}


bool testQStringAssignment()
{
    const char* message = "Hello World";
    uiString string;
    string.setFrom( QString( message ) );

    BufferString res = string.getFullString();
    mRunStandardTest( res==message, "QString assignment" );

    return true;
}


bool testNumberStrings()
{
    /* These tests will fail miserably if a non-C LOCALE is set.

    uiString string = toUiString( 0.9, 3 );
    QString qstr = string.getQString();
    BufferString bstr( qstr );
    mRunStandardTest( bstr=="0.9", "Number string" );

    ArrPtrMan<wchar_t> wbuf = string.createWCharString();
    qstr = QString::fromWCharArray( wbuf );
    bstr = BufferString( qstr );
    mRunStandardTest( bstr=="0.900", "Number string from wchar" );
    */

    return true;
}


bool testLargeNumberStrings()
{
    uiString string = toUiString( 12500 );
    QString qstr = string.getQString();
    BufferString bstr( qstr );
    mRunStandardTest( bstr=="12500", "Large number string" );

    ArrPtrMan<wchar_t> wbuf = string.createWCharString();
    qstr = QString::fromWCharArray( wbuf );
    bstr = BufferString( qstr );
    mRunStandardTest( bstr=="12500", "Large number string from wchar" );

    return true;
}


bool testToLower()
{
    uiString string = uiStrings::phrJoinStrings( uiStrings::sInput(),
					    uiStrings::sHorizon().toLower() );
    BufferString bstr = string.getFullString();
    mRunStandardTest( bstr=="Input horizon", "To lower" );

    uiString string2 = uiStrings::phrJoinStrings( uiStrings::sInput(),
						 uiStrings::sHorizon() );
    bstr = string2.getFullString();
    mRunStandardTest( bstr=="Input Horizon", "To lower does not affect orig" );


    return true;
}


bool testOptionStrings()
{
    uiString strings[] = { toUiString( "One" ), toUiString( "Two" ),
			    toUiString( "Three" ), toUiString( "Four" ),
			    uiString() };
    uiStringSet options( strings );

    mRunStandardTest(
	    options.createOptionString( true, -1, false ).getFullString()==
	      "One, Two, Three and Four", "createOptionString and" );
    mRunStandardTest(
	    options.createOptionString( false, -1, false ).getFullString()==
	      "One, Two, Three or Four", "createOptionString or" );

    mRunStandardTest(
	    options.createOptionString( false, 3, false ).getFullString()==
	      "One, Two, Three or ...", "createOptionString limited" );

    mRunStandardTest(
	    options.createOptionString( true, -1, true ).getFullString()==
	      "One\nTwo\nThree and\nFour", "createOptionString nl and" );
    mRunStandardTest(
	    options.createOptionString( false, -1, true ).getFullString()==
	      "One\nTwo\nThree or\nFour", "createOptionString nl or" );

    mRunStandardTest(
	    options.createOptionString( false, 3, true ).getFullString()==
	      "One\nTwo\nThree\nor ...", "createOptionString nl limited" );


    return true;
}


bool testHexEncoding()
{
    uiString str;
    mRunStandardTest( str.setFromHexEncoded("517420697320677265617421") &&
	              str.getFullString()=="Qt is great!",
		      "Reading hard-coded string" );


    BufferString msg( "Espana" );
    ((unsigned char*) msg.buf() )[4] = 241;

    uiString original( toUiString(msg) );
    BufferString encoding;
    original.getHexEncoded( encoding );

    mRunStandardTest( str.setFromHexEncoded( encoding ) &&
		      original.getQString()==str.getQString(),
		      "Reading encoded string" );

    return true;
}

bool fromBufferStringSetToUiStringSet()
{
    BufferStringSet strset;
    strset.add("A");
    strset.add("B");
    strset.add("C");
    uiStringSet uistrset = strset.getUiStringSet();

    BufferString str = strset.cat( " " );
    uiString uistr = uistrset.cat( uiString::WithSpace );

    mRunStandardTest( str == uistr.getFullString(), "Comparing BuffStrSet "
				    "UiStrSet" );
    return true;

}


int testMain( int argc, char** argv )
{
    mInitTestProg();

    if ( !testArg() || !testSharedData() || !testQStringAssignment() ||
	 !testOptionStrings() || !testHexEncoding() || !testIsEqual() ||
	 !testSetEmpty() || !testNumberStrings() || !testLargeNumberStrings() ||
	 !testToLower() || !TestTranslator::testTranslation() ||
	 !fromBufferStringSetToUiStringSet() )
	return 1;

    return 0;
}
