/*! \file _build.h
 * \brief Build identification and version info.
 *
 */

#ifndef MUX_BUILD_NUM
extern char szBuildNum[];
#define MUX_BUILD_NUM szBuildNum
#endif // MUX_BUILD_NUM

#ifndef MUX_BUILD_DATE
extern char szBuildDate[];
#define MUX_BUILD_DATE szBuildDate
#endif // MUX_BUILD_DATE

<<<<<<< HEAD
#define MUX_VERSION       "2.12.0.4-MP"         // Version number
#define MUX_RELEASE_DATE  "2019-NOV-14"      // Source release date
=======
#define MUX_VERSION       "2.13.0.0"         // Version number
#define MUX_RELEASE_DATE  "2020-JUN-30"      // Source release date

// Define if this release is qualified as ALPHA or BETA.
//
#define ALPHA
//#define BETA
>>>>>>> f3961001ac4da0a421fbd4f7a3616c0c501f81ff
