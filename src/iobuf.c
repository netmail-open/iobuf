#include<iobuf.h>
#include<xplfile.h>

#ifndef min
#define min(a, b)   (((a) < (b))? (a): (b))
#endif

#ifndef max
#define max(a, b)   (((a) > (b))? (a): (b))
#endif


// iob->end is now a valid pointer, it always contains a '\0'
EXPORT IOBuffer *IOBAllocEx( size_t size, char *file, int line )
{
	IOBuffer *iob;

	if (!size) {
		/*
			0 requests the default size.  Asking for 3k will result in getting
			entry from the 4k pool.  Because of the overhead of MemoryManager
			asking for 4k would get an entry from the 8k pool.
		*/
		size = (3 * 1024);
	} else  {
		if (size < 128) {
			/* Enforce a sane minimum buffer size */
			size = 128;
		}

		/*
			If a specific size was asked for then the consumer may rely on being
			able to hold that ammount.
		*/
		size += sizeof(IOBuffer);
	}

	if ((iob = MemMallocEx(NULL, size, &size, TRUE, FALSE))) {
		MemUpdateOwner(iob, file, line);
		memset(iob, 0, sizeof(IOBuffer));
		iob->end = iob->buffer + (size - sizeof(IOBuffer));
		IOBInit(iob);
	}
	return iob;
}

EXPORT int IOBReallocEx( IOBuffer **iob, size_t size, char *file, int line )
{
	IOBuffer	*newBuffer;
	char		*data;
	int			bytes;

	if( iob )
	{
		if( newBuffer = IOBAllocEx( size, file, line ) )
		{
			while( bytes = IOBGetUsed( *iob, &data ) )
			{
				if( bytes != IOBWrite( newBuffer, data, bytes ) )
				{
					// copy failure
					IOBFree( newBuffer );
					return -EMSGSIZE;
				}
			}
			// all data transferred to new buffer
			newBuffer->fillCB = (*iob)->fillCB;
			newBuffer->fillHandle = (*iob)->fillHandle;
			newBuffer->flushCB = (*iob)->flushCB;
			newBuffer->flushHandle = (*iob)->flushHandle;
			IOBFree( *iob );
			*iob = newBuffer;
			return 0;
		}
	}
	return -EINVAL;
}

EXPORT void IOBFree( IOBuffer *iob )
{
	if( iob )
	{
		MemFree( iob );
	}
}

EXPORT int IOBInit( IOBuffer *iob )
{
	if( iob )
	{
		memset( iob->buffer, 0, (iob->end - iob->buffer) + 1 );
		iob->r = iob->w = iob->buffer;
		return 0;
	}
	return -1;
}

// returns buffer free space
// data points at first free byte
static int _IOBGetFree( IOBuffer *iob, char **data )
{
	DebugAssert( *( iob->end ) == '\0' );
	if( iob->w == iob->end )
	{
		if( iob->precious )
		{
			*data = NULL;
			return 0;
		}
		if( iob->r == iob->end )
		{
			iob->r = iob->w = iob->buffer;
			*data = iob->w;
			return iob->end - iob->w;
		}
		else if( iob->r > iob->buffer )
		{
			iob->w = iob->buffer;
			*data = iob->w;
			return (iob->r - iob->w) - 1;
		}
		else
		{
			// buffer full
			*data = NULL;
			return 0;
		}
	}
	else if( iob->w < iob->r )
	{
		*data = iob->w;
		return (iob->r - iob->w) - 1;
	}
	else if( iob->w == iob->r )
	{
		if( !iob->precious )
		{
			iob->r = iob->w = iob->buffer;
		}
	}
	*data = iob->w;
	return iob->end - iob->w;
}

EXPORT int IOBGetFree( IOBuffer *iob, char **data )
{
	int len;

	len = _IOBGetFree( iob, data );
	if( len )
	{
		return len;
	}
	IOBFlush( iob );
	return _IOBGetFree( iob, data );
}

// marks buffer space as being used
EXPORT int IOBMarkUsed( IOBuffer *iob, int len )
{
	int l1;

	DebugAssert( *( iob->end ) == '\0' );
	if( iob->w < iob->r )
	{
		l1 = (iob->r - iob->w) - 1;
	}
	else
	{
		l1 = iob->end - iob->w;
	}
	if( len > l1 )
	{
		XplConsolePrintf( "IOB: IOBMarkUsed overflow\n" );
#ifdef DEBUG
		*((char *)NULL) = '\0';
#endif
		iob->w += l1;
		*iob->w = '\0';
		return l1;
	}
	iob->w += len;
	*iob->w = '\0';
	return len;
}

// returns buffer used space
// data points at first used byte
static int _IOBGetUsed( IOBuffer *iob, char **data )
{
	if( iob->r == iob->w )
	{
		if( !iob->precious )
		{
			iob->r = iob->w = iob->buffer;
		}
		// buffer empty
		if( data )
		{
			*data = NULL;
		}
		return 0;
	}
	if( iob->r == iob->end )
	{
		if( iob->precious )
		{
			*data = NULL;
			return 0;
		}

		iob->r = iob->buffer;
		if( iob->r == iob->w )
		{
			// buffer empty
			if( data )
			{
				*data = NULL;
			}
			return 0;
		}
	}
	if( data )
	{
		*data = iob->r;
	}
	if( iob->r < iob->w )
	{
		DebugAssert( *( iob->w ) == '\0' );
		return iob->w - iob->r;
	}
	DebugAssert( *( iob->end ) == '\0' );
	return iob->end - iob->r;
}

EXPORT int IOBFill( IOBuffer *iob )
{
	int len, l1;
	char *fData;

	if( iob->fillCB )
	{
		while( ( len = _IOBGetFree( iob, &fData ) ) )
		{
			if( l1 = iob->fillCB( iob->fillHandle, fData, len ) )
			{
				IOBMarkUsed( iob, l1 );
			}
			if( l1 < len )
			{
				break;
			}
		}
		return _IOBGetUsed( iob, &fData );
	}
	return 0;
}

EXPORT int IOBGetUsed( IOBuffer *iob, char **data )
{
	int len;

	len = _IOBGetUsed( iob, data );
	if( len )
	{
		return len;
	}
	IOBFill( iob );
	return _IOBGetUsed( iob, data );
}

// marks buffer space as being free
EXPORT int IOBMarkFree( IOBuffer *iob, int len )
{
	int l1;

	DebugAssert( *( iob->end ) == '\0' );
	if( iob->r <= iob->w )
	{
		l1 = iob->w - iob->r;
	}
	else
	{
		l1 = iob->end - iob->r;
	}
	if( len > l1 )
	{
		XplConsolePrintf( "IOB: IOBMarkFree overflow\n" );
#ifdef DEBUG
		*((char *)NULL) = '\0';
#endif
		iob->r += l1;
		return l1;
	}
	iob->r += len;
	if( !iob->precious )
	{
		// the previously read data is not precious, we can overwrite it
		if( iob->r == iob->w )
		{
			iob->r = iob->w = iob->buffer;
			*iob->buffer = '\0';
		}
	}
	return len;
}

// write to buffer from data
// returns number of bytes transfered
EXPORT int IOBWrite( IOBuffer *iob, char *data, int len )
{
	int l1, bytes;
	char *buff;

	bytes = 0;
	while( len )
	{
		if( ( l1 = IOBGetFree( iob, &buff ) ) )
		{
			l1 = min( len, l1 );
			memcpy( buff, data + bytes, l1 );
			IOBMarkUsed( iob, l1 );
			len -= l1;
			bytes += l1;
		}
		else
		{
			return bytes;
		}
	}
	return bytes;
}

// IOB write that can be used for a WJW callback
// remember to call IOBFlush() after WJWCloseDocument()
EXPORT size_t IOBWriteWJ( char *buffer, size_t length, void *handle )
{
	DebugAssert( buffer );
	DebugAssert( handle );
	if( handle )
	{
		return IOBWrite( (IOBuffer *)handle, buffer, length );
	}
	return 0;
}

// read from buffer to data pointer
// returns number of bytes transfered
EXPORT int IOBRead( IOBuffer *iob, char *data, int len )
{
	int l1, bytes;
	char *buff;

	bytes = 0;
	while( len )
	{
		if( ( l1 = IOBGetUsed( iob, &buff ) ) )
		{
			l1 = min( len, l1 );
			memcpy( data + bytes, buff, l1 );
			IOBMarkFree( iob, l1 );
			len -= l1;
			bytes += l1;
		}
		else
		{
			return bytes;
		}
	}
	return bytes;
}

// IOB read that can be used as a WJR callback
EXPORT size_t IOBReadWJ( char *buffer, size_t length, size_t seen, void *handle )
{
	DebugAssert( buffer );
	DebugAssert( length );
	DebugAssert( handle );
	if( handle )
	{
		return IOBRead( (IOBuffer *)handle, buffer, length );
	}
	return 0;
}

// returns the bytes left in the buffer after trying to flush it with the flush callback
EXPORT int IOBFlush( IOBuffer *iob )
{
	int len, l1;
	char *fData;

	if( iob->flushCB )
	{
		while( ( len = _IOBGetUsed( iob, &fData ) ) )
		{
			l1 = iob->flushCB( iob->flushHandle, fData, len );
			if( l1 < len )
			{
				IOBMarkFree( iob, l1 );
				IOBDefrag( iob );
				if( len = _IOBGetUsed( iob, &fData ) )
				{
					l1 = iob->flushCB( iob->flushHandle, fData, len );
				}
			}
			IOBMarkFree( iob, l1 );
			if( l1 < len )
			{
				break;
			}
		}
	}
	return _IOBGetUsed( iob, &fData );
}

EXPORT size_t IOBRun( IOBuffer *iob )
{
	int		len;
	size_t	bytes;
	char	*unused;

	bytes = 0;
	if( iob->fillCB && iob->flushCB )
	{
		while( len = IOBGetUsed( iob, &unused ) )
		{
			IOBFlush( iob );
			bytes += len;
		}
		if( IOBFlush( iob ) )
		{
			errno = EPIPE;
			return 0;
		}
	}
	return bytes;
}

EXPORT int IOBSetFlushCB( IOBuffer *iob, int (*flushCB)(void *, char *, int), void *flushHandle )
{
	if( iob )
	{
		iob->flushCB = flushCB;
		iob->flushHandle = flushHandle;
		return 0;
	}
	return -1;
}

EXPORT int IOBSetFillCB( IOBuffer *iob, int (*fillCB)(void *, char *, int), void *fillHandle )
{
	if( iob )
	{
		iob->fillCB = fillCB;
		iob->fillHandle = fillHandle;
		return 0;
	}
	return -1;
}

static void _IOBIncreaseFree( IOBuffer *iob )
{
	char *p;
	int len;

	if( iob->precious )
	{
		return;
	}

	p = iob->buffer;
	while( iob->r < iob->w )
	{
		len = min( iob->w - iob->r, iob->r - p );
		memcpy( p, iob->r, len );
		p += len;
		iob->r += len;
	}
	iob->r = iob->buffer;
	iob->w = p;
	*iob->w = '\0';
}

static int _IOBDefrag( IOBuffer *iob )
{
	int len, xLen;
	char *xfer, *p;

	xLen = (iob->w - iob->buffer);
	if( xfer = (char *)MemMalloc( xLen ) )
	{
		memcpy( xfer, iob->buffer, xLen );
		p = iob->buffer;
		while( iob->r < iob->end )
		{
			len = min( iob->end - iob->r, iob->r - p );
			memcpy( p, iob->r, len );
			p += len;
			iob->r += len;
		}
		memcpy( p, xfer, xLen );
		iob->r = iob->buffer;
		iob->w = p + xLen;
		*iob->w = '\0';
		MemFree( xfer );
		return 1;
	}
	return 0;
}

EXPORT int IOBDefrag( IOBuffer *iob )
{
	if( iob->w < iob->r )
	{	/* buffer is fragged */
		return _IOBDefrag( iob );
	}

	/* buffer is not fragged */
	if( ( iob->r != iob->buffer ) && ( iob->w == iob->end ) )
	{	/*
		   There is no more room at the end of the buffer.
		   any additional data written to this iob will cause it
		   to become fragmented. Since defrag is called because
		   more contiguous data is wanted, the data needs to be moved.
		*/
		_IOBIncreaseFree( iob );
	}
	return 0;
}

EXPORT int IOBIncreaseFree( IOBuffer *iob )
{
	if( ( iob->w < iob->r ) || ( iob->r == iob->buffer ) )
	{	/* free space is already maximized */
		return 0;
	}

	_IOBIncreaseFree( iob );
	return 1;
}

EXPORT int MemSearch( char *buffer, int len, char *pattern, int patternLen, int flags, char **match, int *matchLen )
{
	int l;
	char *p, *s, *lp;

	if( ( flags & SEARCH_ANY_CR ) && ( !pattern || !patternLen ) )
	{
		/* A pattern is not required for SEARCH_ANY_CR */
		pattern = "\r\n";
		patternLen = 2;
	}

	if( !buffer || !len || !pattern || !patternLen )
	{
		if (match) *match = NULL;
		if (matchLen) *matchLen = 0;
		return 0;
	}

	l = 0;
	s = NULL;
	lp = NULL;
	for(p=buffer;p<buffer+len;p++)
	{
		if( flags & SEARCH_ANY_CR )
		{
			if( '\r' == *p )
			{
				if( !s )    s = p;
			}
			else if( '\n' == *p )
			{
				if( !s )
				{
					s = p;
				}
				if (match)		*match = s;
				if (matchLen)	*matchLen = (p - s) + 1;
				return s - buffer;
			}
			else if( s )	// current data is not \r or \n and we have had some \r
			{
				if (match)		*match = s;
				if (matchLen)	*matchLen = 1;
				return s - buffer;
			}
			continue;
		}
		if( ( *p == '\r' ) && ( flags & SEARCH_IGNORE_CR ) )
		{
			if( !s )
			{
				s = p;
			}
			continue;
		}
		if( ( !*p ) && ( flags & SEARCH_REPLACE_NULL ) )
		{
			*p = ' ';
		}
		if( *p == pattern[l] )
		{
			lp = p;
			if( !s )
			{
				s = p;
			}
			l++;
			if( l == patternLen )
			{
				if (match) *match = s;
				if (matchLen) *matchLen = (p - s) + 1;
				return s - buffer;
			}
		}
		else
		{
			if( lp )
			{
				p = lp;
			}
			s = NULL;
			l = 0;
			lp = 0;
		}
	}
	if (match) *match = NULL;
	if (matchLen) *matchLen = 0;
	if( s )
	{
		// partial match
		return s - buffer;
	}
	return len;
}

EXPORT int IOBSearch( IOBuffer *iob, char *pattern, int flags, char **data, char **match, int *matchLen )
{
	int rLen, dLen, nLen, mLen;
	char *m;

	do {
		dLen = IOBGetUsed( iob, data );
		rLen = MemSearch( *data, dLen, pattern, pattern ? strlen( pattern ) : 0, flags, &m, &mLen );
		if( m ) break; /* match!! */

		/* check for fragmented iob */
		if( IOBDefrag( iob ) )
		{	/* fragments have been combined; try again.*/
			dLen = IOBGetUsed( iob, data );
			rLen = MemSearch( *data, dLen, pattern, pattern ? strlen( pattern ) : 0, flags, &m, &mLen );
			if( m ) break; /* match!! */
		}
		/* call fill cb to see if more data is available */
		IOBFill( iob );
		nLen = IOBGetUsed( iob, data );
		if( nLen > dLen )
		{	/* more data is available */
			dLen = nLen;
			rLen = MemSearch( *data, dLen, pattern, pattern ? strlen( pattern ) : 0, flags, &m, &mLen );
			if( m ) break; /* match!! */

			/* check fill data for fragmentation */
			if( IOBDefrag( iob ) )
			{	/* fragments have been combined; try again.*/
				dLen = IOBGetUsed( iob, data );
				rLen = MemSearch( *data, dLen, pattern, pattern ? strlen( pattern ) : 0, flags, &m, &mLen );
				if( m ) break; /* match!! */
			}
		}
	} while( FALSE );

	if( match ) *match = m;
	if( matchLen ) *matchLen = mLen;
	return rLen;
}


__inline static void
consolePrintBuffer( char *start, int size )
{
	char *buff;

	buff = MemMalloc( size + 1 );
	if( buff ) {
		memcpy( buff, start, size );
		buff[ size ] = '\0';
		XplConsolePrintf("%s", buff );
		MemFree( buff );
	}
}

EXPORT void IOBPrint( IOBuffer *iob )
{
	if( iob->w > iob->r ) {
		consolePrintBuffer( iob->r, iob->w - iob->r );
	} else if( iob->r > iob->w ) {
		consolePrintBuffer( iob->r, iob->end - iob->r );
		consolePrintBuffer( iob->buffer, iob->w - iob->buffer );
	}
}

EXPORT XplBool IOBLog( IOBuffer *iob, char *msg, int size )
{
	int count;
	char *buff;
	char *delim;
	int delimLen;

	do {
		count = IOBGetFree( iob, &buff );
		while( count ) {
			if ( count > size ) {
				memcpy( buff, msg, size );
				IOBMarkUsed( iob, size );
				return( TRUE );
			}
			memcpy( buff, msg, count );
			IOBMarkUsed( iob, count );
			msg += count;
			size -= count;
			count = IOBGetFree( iob, &buff );
		}

		/* the buffer is full, free a line */
		count = IOBSearch( iob, "\n", 0, &buff, &delim, &delimLen );
		if( !delim ) {
			break;
		}
		IOBMarkFree( iob, count + delimLen );
	} while( TRUE );
	/* the message won't fit in the buffer */
	return( FALSE );
}

#if 0
static int __fseek( FILE *fp, long int offset, int where, long line )
{
	if( fseek( fp, offset, where ) )
	{
		printf( "Seek: %d - %d\n", errno, line );
		return -1;
	}
	return 0;
}

static long int __ftell( FILE *fp, long line )
{
	long int offset;

	offset = ftell( fp );
	if( -1 == offset )
	{
		printf( "Tell: %d - %d\n", errno, line );
	}
	return offset;
}

#define _fseek( f, o, w )	__fseek( (f), (o), (w), __LINE__ );
#define _ftell( f )			__ftell( (f), __LINE__ );
#endif



static int IOBFileFlush( IOBFile *iof, char *data, int size )
{
	int bytes;

	if( !iof->fp )
	{
		iof->fp = fopen( iof->fileName, "wb+" );
		if( !iof->fp )
		{
			DebugAssert(0);
			// return -1 here
			// change stuff so flush errors error the iob
			return 0;
		}
		// mark iob as not precious, the data is in the file now
		iof->iob->precious = 0;
	}
	else
	{
		XplFSeek( iof->fp, iof->writeP, SEEK_SET );
	}
	bytes = fwrite( data, 1, size, iof->fp );
	iof->writeP = XplFTell( iof->fp );
	return bytes;
}

static int IOBFileFill( IOBFile *iof, char *data, int size )
{
	int bytes;

	if( !iof->fp )
	{
		if( iof->fp = XplFOpen( iof->fileName, "rb+" ) )
		{
			XplFSeek( iof->fp, 0, SEEK_END );
			iof->writeP = XplFTell( iof->fp );
			XplFRewind( iof->fp );
			DebugAssert( !ferror( iof->fp ) );
			iof->readP = XplFTell( iof->fp );
			DebugAssert( !ferror( iof->fp ) );
		}
	}
	else
	{
		DebugAssert( !ferror( iof->fp ) );
		XplFSeek( iof->fp, iof->readP, SEEK_SET );
		DebugAssert( !ferror( iof->fp ) );
	}
	if( iof->fp )
	{
		bytes = fread( data, 1, size, iof->fp );
		iof->readP = XplFTell( iof->fp );
		DebugAssert( bytes || feof( iof->fp ) );
		DebugAssert( !ferror( iof->fp ) );
		return bytes;
	}
	return 0;
}

EXPORT IOBFile *IOBFileOpenEx( char *fileName, int bufferSize, const char *file, const int line )
{
	IOBFile *iof;

	if( fileName )
	{
		if( iof = (IOBFile *)MemMalloc( sizeof( IOBFile ) + strlen( fileName ) + 1 ) )
		{
			MemUpdateOwner( iof, file, line );
			memset( iof, 0, sizeof( IOBFile ) );
			iof->state = IOF_WRITE;
			strcpy( iof->fileName, fileName );
			if( iof->fp = XplFOpen( iof->fileName, "rb+" ) )
			{
				XplFSeek( iof->fp, 0, SEEK_END );
				iof->writeP = XplFTell( iof->fp );
			}
			if( iof->iob = IOBAlloc( bufferSize ) )
			{
				MemUpdateOwner( iof->iob, file, line );
				// mark iob as precious, we can then rewind and re-read the iob if it hasn't been flushed
				iof->iob->precious = 1;
				IOBSetFlushCB( iof->iob, (int(*)(void *, char *, int ))IOBFileFlush, iof );
				IOBSetFillCB( iof->iob, (int(*)(void *, char *, int ))IOBFileFill, iof );
				return iof;
			}
			MemFree( iof );
		}
	}
	return NULL;
}

EXPORT IOBFile *IOBFileCreateEx( char *fileName, int bufferSize, const char *file, const int line )
{
	IOBFile *iof;

	if( fileName )
	{
		if( iof = (IOBFile *)MemMalloc( sizeof( IOBFile ) + strlen( fileName ) + 1 ) )
		{
			MemUpdateOwner( iof, file, line );
			memset( iof, 0, sizeof( IOBFile ) );
			iof->state = IOF_WRITE;
			strcpy( iof->fileName, fileName );
			unlink( fileName );
			if( iof->iob = IOBAlloc( bufferSize ) )
			{
				MemUpdateOwner( iof->iob, file, line );
				// mark iob as precious, we can then rewind and re-read the iob if it hasn't been flushed
				iof->iob->precious = 1;
				IOBSetFlushCB( iof->iob, (int(*)(void *, char *, int ))IOBFileFlush, iof );
				IOBSetFillCB( iof->iob, (int(*)(void *, char *, int ))IOBFileFill, iof );
				return iof;
			}
			MemFree( iof );
		}
	}
	return NULL;
}

EXPORT int IOBFileClose( IOBFile *iof, int keep )
{
	if( iof )
	{
		if( keep )
		{
			IOBFlush( iof->iob );
		}
		if( iof->fp )
		{
			fclose( iof->fp );
			if( !keep )
			{
				unlink( iof->fileName );
			}
		}
		IOBFree( iof->iob );
		MemFree( iof );
		return 0;
	}
	return -1;
}

EXPORT int IOBFileWrite( IOBFile *iof, char *data, int len )
{
	if( IOF_WRITE == iof->state )
	{
		return IOBWrite( iof->iob, data, len );
	}
	printf( "IOFFileWrite() transition needs to be implemented\n" );
	return 0;
}

static void _ReadTransition( IOBFile *iof )
{
	if( IOF_WRITE == iof->state )
	{
		if( iof->fp )
		{
			IOBFlush( iof->iob );
		}
	}
	iof->state = IOF_READ;
}

EXPORT int IOBFileRead( IOBFile *iof, char *data, int len )
{
	_ReadTransition( iof );
	return IOBRead( iof->iob, data, len );
}

EXPORT int IOBFilePeek( IOBFile *iof, char **data )
{
	_ReadTransition( iof );
	return IOBGetUsed( iof->iob, data );
}

EXPORT int IOBFileSeen( IOBFile *iof, int len )
{
	_ReadTransition( iof );
	return IOBMarkFree( iof->iob, len );
}

EXPORT int IOBFileSearch( IOBFile *iof, char *pattern, int flags, char **data, char **match, int *matchLen )
{
	_ReadTransition( iof );
	return IOBSearch( iof->iob, pattern, flags, data, match, matchLen );
}

EXPORT int IOBFileRewind( IOBFile *iof )
{
	_ReadTransition( iof );
	if( iof->fp )
	{
		DebugAssert( !ferror( iof->fp ) );
		XplFRewind( iof->fp );
		DebugAssert( !ferror( iof->fp ) );
		iof->readP = XplFTell( iof->fp );
		iof->iob->r = iof->iob->w = iof->iob->buffer;
		IOBFill( iof->iob );
	}
	else
	{
		iof->iob->r = iof->iob->buffer;
	}
	return 0;
}

EXPORT long IOBFileSize( IOBFile *iof )
{
	_ReadTransition( iof );
	if( iof->fp )
	{
		return iof->writeP;
	}
	return iof->iob->w - iof->iob->buffer;
}

EXPORT long IOBFileGetPosition( IOBFile *iof )
{
	_ReadTransition( iof );
	if( iof->fp )
	{
		return iof->readP - IOBGetUsed( iof->iob, NULL );
	}
	return iof->iob->r - iof->iob->buffer;
}

EXPORT int IOBFileSetPosition( IOBFile *iof, long position )
{
	_ReadTransition( iof );
	if( iof->fp )
	{
		iof->readP = position;
		iof->iob->r = iof->iob->w = iof->iob->buffer;
		IOBFill( iof->iob );
	}
	else
	{
		iof->iob->r = iof->iob->buffer + position;
	}
	return 0;
}

