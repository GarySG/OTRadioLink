/*
//The OpenTRV project licenses this file to you
under the Apache Licence, Version 2.0 (the "Licence");
you may not use this file except in compliance
with the Licence. You may obtain a copy of the Licence at

http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing,
software distributed under the Licence is distributed on an
"AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
KIND, either express or implied. See the Licence for the
specific language governing permissions and limitations
under the Licence.

Author(s) / Copyright (s): Damon Hart-Davis 2016
*/

/*
 Basic compatibility support for Arduino and non-Arduino environments.
 */

#ifndef OTV0P2BASE_ARDUINOCOMPAT_H
#define OTV0P2BASE_ARDUINOCOMPAT_H


namespace OTV0P2BASE
{

#ifndef ARDUINO

// Enable minimal elements to support cross-compilation.

// F() macro on Arduino for hosting string constant in Flash, eg print(F("no space taken in RAM")).
#ifndef F
class __FlashStringHelper;
#define F(string_literal) (reinterpret_cast<const __FlashStringHelper *>(string_literal))
// Arduino original:
//#define F(string_literal) (reinterpret_cast<const __FlashStringHelper *>(PSTR(string_literal)))
#endif

#endif

}

#endif
