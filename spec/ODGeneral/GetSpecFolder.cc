/*+
 * AUTHOR   : A.H. Lammertink
 * DATE     : October 2003
 * FUNCTION : get special folder location
-*/

static const char* rcsID = "$Id: GetSpecFolder.cc,v 1.2 2004-01-22 09:24:31 dgb Exp $";


#include "prog.h"
#include "getspec.h"

#include <iostream>


int main( int argc, char** argv )
{
    if ( argc < 2 )
    {
	cerr << "Usage: " << argv[0]
	     << " {MyDocs,AppData,UsrProf,CommonApp,CommonDoc}"<< endl;
	return 1;
    }

    if ( !strcmp(argv[1], "MyDocs") )
    {
	cout <<  GetSpecialFolderLocation( CSIDL_PERSONAL ) << endl;
	return 0;
    }
    else if ( !strcmp(argv[1], "AppData") )
    {
	cout <<  GetSpecialFolderLocation( CSIDL_APPDATA ) << endl;
	return 0;
    }
    else if ( !strcmp(argv[1], "UsrProf") )
    {
	cout <<  GetSpecialFolderLocation( CSIDL_PROFILE ) << endl;
	return 0;
    }
    else if ( !strcmp(argv[1], "CommonApp") )
    {
	cout <<  GetSpecialFolderLocation( CSIDL_COMMON_APPDATA ) << endl;
	return 0;
    }
    else if ( !strcmp(argv[1], "CommonDoc") )
    {
	cout <<  GetSpecialFolderLocation( CSIDL_COMMON_DOCUMENTS ) << endl;
	return 0;
    }

    return 1;
}

