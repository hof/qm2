/**
 * Maxima, a chess playing program. 
 * Copyright (C) 1996-2014 Erik van het Hof and Hermen Reitsma 
 * 
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 3
 * of the License, or (at your option) any later version.
 *  
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *  
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, If not, see <http://www.gnu.org/licenses/>.
 * 
 * 
 * File: book.cpp
 * Read Polyglot format opening books
 * 
 * The code in this file is based on the opening book code in PolyGlot
 * by Fabien Letouzey. PolyGlot is available under the GNU General
 * Public License, and can be downloaded from http://wbec-ridderkerk.nl
 */

#include <cstdlib>
#include "book.h"
#include "movegen.h"
#include "bbmoves.h"

using namespace std;

namespace {

    // Book entry size in bytes
    const int EntrySize = 16;

    // Random numbers from PolyGlot, used to compute book hash keys
    const U64 Random64[781] = {
        C64(0x9D39247E33776D41), C64(0x2AF7398005AAA5C7), C64(0x44DB015024623547), C64(0x9C15F73E62A76AE2),
        C64(0x75834465489C0C89), C64(0x3290AC3A203001BF), C64(0x0FBBAD1F61042279), C64(0xE83A908FF2FB60CA),
        C64(0x0D7E765D58755C10), C64(0x1A083822CEAFE02D), C64(0x9605D5F0E25EC3B0), C64(0xD021FF5CD13A2ED5),
        C64(0x40BDF15D4A672E32), C64(0x011355146FD56395), C64(0x5DB4832046F3D9E5), C64(0x239F8B2D7FF719CC),
        C64(0x05D1A1AE85B49AA1), C64(0x679F848F6E8FC971), C64(0x7449BBFF801FED0B), C64(0x7D11CDB1C3B7ADF0),
        C64(0x82C7709E781EB7CC), C64(0xF3218F1C9510786C), C64(0x331478F3AF51BBE6), C64(0x4BB38DE5E7219443),
        C64(0xAA649C6EBCFD50FC), C64(0x8DBD98A352AFD40B), C64(0x87D2074B81D79217), C64(0x19F3C751D3E92AE1),
        C64(0xB4AB30F062B19ABF), C64(0x7B0500AC42047AC4), C64(0xC9452CA81A09D85D), C64(0x24AA6C514DA27500),
        C64(0x4C9F34427501B447), C64(0x14A68FD73C910841), C64(0xA71B9B83461CBD93), C64(0x03488B95B0F1850F),
        C64(0x637B2B34FF93C040), C64(0x09D1BC9A3DD90A94), C64(0x3575668334A1DD3B), C64(0x735E2B97A4C45A23),
        C64(0x18727070F1BD400B), C64(0x1FCBACD259BF02E7), C64(0xD310A7C2CE9B6555), C64(0xBF983FE0FE5D8244),
        C64(0x9F74D14F7454A824), C64(0x51EBDC4AB9BA3035), C64(0x5C82C505DB9AB0FA), C64(0xFCF7FE8A3430B241),
        C64(0x3253A729B9BA3DDE), C64(0x8C74C368081B3075), C64(0xB9BC6C87167C33E7), C64(0x7EF48F2B83024E20),
        C64(0x11D505D4C351BD7F), C64(0x6568FCA92C76A243), C64(0x4DE0B0F40F32A7B8), C64(0x96D693460CC37E5D),
        C64(0x42E240CB63689F2F), C64(0x6D2BDCDAE2919661), C64(0x42880B0236E4D951), C64(0x5F0F4A5898171BB6),
        C64(0x39F890F579F92F88), C64(0x93C5B5F47356388B), C64(0x63DC359D8D231B78), C64(0xEC16CA8AEA98AD76),
        C64(0x5355F900C2A82DC7), C64(0x07FB9F855A997142), C64(0x5093417AA8A7ED5E), C64(0x7BCBC38DA25A7F3C),
        C64(0x19FC8A768CF4B6D4), C64(0x637A7780DECFC0D9), C64(0x8249A47AEE0E41F7), C64(0x79AD695501E7D1E8),
        C64(0x14ACBAF4777D5776), C64(0xF145B6BECCDEA195), C64(0xDABF2AC8201752FC), C64(0x24C3C94DF9C8D3F6),
        C64(0xBB6E2924F03912EA), C64(0x0CE26C0B95C980D9), C64(0xA49CD132BFBF7CC4), C64(0xE99D662AF4243939),
        C64(0x27E6AD7891165C3F), C64(0x8535F040B9744FF1), C64(0x54B3F4FA5F40D873), C64(0x72B12C32127FED2B),
        C64(0xEE954D3C7B411F47), C64(0x9A85AC909A24EAA1), C64(0x70AC4CD9F04F21F5), C64(0xF9B89D3E99A075C2),
        C64(0x87B3E2B2B5C907B1), C64(0xA366E5B8C54F48B8), C64(0xAE4A9346CC3F7CF2), C64(0x1920C04D47267BBD),
        C64(0x87BF02C6B49E2AE9), C64(0x092237AC237F3859), C64(0xFF07F64EF8ED14D0), C64(0x8DE8DCA9F03CC54E),
        C64(0x9C1633264DB49C89), C64(0xB3F22C3D0B0B38ED), C64(0x390E5FB44D01144B), C64(0x5BFEA5B4712768E9),
        C64(0x1E1032911FA78984), C64(0x9A74ACB964E78CB3), C64(0x4F80F7A035DAFB04), C64(0x6304D09A0B3738C4),
        C64(0x2171E64683023A08), C64(0x5B9B63EB9CEFF80C), C64(0x506AACF489889342), C64(0x1881AFC9A3A701D6),
        C64(0x6503080440750644), C64(0xDFD395339CDBF4A7), C64(0xEF927DBCF00C20F2), C64(0x7B32F7D1E03680EC),
        C64(0xB9FD7620E7316243), C64(0x05A7E8A57DB91B77), C64(0xB5889C6E15630A75), C64(0x4A750A09CE9573F7),
        C64(0xCF464CEC899A2F8A), C64(0xF538639CE705B824), C64(0x3C79A0FF5580EF7F), C64(0xEDE6C87F8477609D),
        C64(0x799E81F05BC93F31), C64(0x86536B8CF3428A8C), C64(0x97D7374C60087B73), C64(0xA246637CFF328532),
        C64(0x043FCAE60CC0EBA0), C64(0x920E449535DD359E), C64(0x70EB093B15B290CC), C64(0x73A1921916591CBD),
        C64(0x56436C9FE1A1AA8D), C64(0xEFAC4B70633B8F81), C64(0xBB215798D45DF7AF), C64(0x45F20042F24F1768),
        C64(0x930F80F4E8EB7462), C64(0xFF6712FFCFD75EA1), C64(0xAE623FD67468AA70), C64(0xDD2C5BC84BC8D8FC),
        C64(0x7EED120D54CF2DD9), C64(0x22FE545401165F1C), C64(0xC91800E98FB99929), C64(0x808BD68E6AC10365),
        C64(0xDEC468145B7605F6), C64(0x1BEDE3A3AEF53302), C64(0x43539603D6C55602), C64(0xAA969B5C691CCB7A),
        C64(0xA87832D392EFEE56), C64(0x65942C7B3C7E11AE), C64(0xDED2D633CAD004F6), C64(0x21F08570F420E565),
        C64(0xB415938D7DA94E3C), C64(0x91B859E59ECB6350), C64(0x10CFF333E0ED804A), C64(0x28AED140BE0BB7DD),
        C64(0xC5CC1D89724FA456), C64(0x5648F680F11A2741), C64(0x2D255069F0B7DAB3), C64(0x9BC5A38EF729ABD4),
        C64(0xEF2F054308F6A2BC), C64(0xAF2042F5CC5C2858), C64(0x480412BAB7F5BE2A), C64(0xAEF3AF4A563DFE43),
        C64(0x19AFE59AE451497F), C64(0x52593803DFF1E840), C64(0xF4F076E65F2CE6F0), C64(0x11379625747D5AF3),
        C64(0xBCE5D2248682C115), C64(0x9DA4243DE836994F), C64(0x066F70B33FE09017), C64(0x4DC4DE189B671A1C),
        C64(0x51039AB7712457C3), C64(0xC07A3F80C31FB4B4), C64(0xB46EE9C5E64A6E7C), C64(0xB3819A42ABE61C87),
        C64(0x21A007933A522A20), C64(0x2DF16F761598AA4F), C64(0x763C4A1371B368FD), C64(0xF793C46702E086A0),
        C64(0xD7288E012AEB8D31), C64(0xDE336A2A4BC1C44B), C64(0x0BF692B38D079F23), C64(0x2C604A7A177326B3),
        C64(0x4850E73E03EB6064), C64(0xCFC447F1E53C8E1B), C64(0xB05CA3F564268D99), C64(0x9AE182C8BC9474E8),
        C64(0xA4FC4BD4FC5558CA), C64(0xE755178D58FC4E76), C64(0x69B97DB1A4C03DFE), C64(0xF9B5B7C4ACC67C96),
        C64(0xFC6A82D64B8655FB), C64(0x9C684CB6C4D24417), C64(0x8EC97D2917456ED0), C64(0x6703DF9D2924E97E),
        C64(0xC547F57E42A7444E), C64(0x78E37644E7CAD29E), C64(0xFE9A44E9362F05FA), C64(0x08BD35CC38336615),
        C64(0x9315E5EB3A129ACE), C64(0x94061B871E04DF75), C64(0xDF1D9F9D784BA010), C64(0x3BBA57B68871B59D),
        C64(0xD2B7ADEEDED1F73F), C64(0xF7A255D83BC373F8), C64(0xD7F4F2448C0CEB81), C64(0xD95BE88CD210FFA7),
        C64(0x336F52F8FF4728E7), C64(0xA74049DAC312AC71), C64(0xA2F61BB6E437FDB5), C64(0x4F2A5CB07F6A35B3),
        C64(0x87D380BDA5BF7859), C64(0x16B9F7E06C453A21), C64(0x7BA2484C8A0FD54E), C64(0xF3A678CAD9A2E38C),
        C64(0x39B0BF7DDE437BA2), C64(0xFCAF55C1BF8A4424), C64(0x18FCF680573FA594), C64(0x4C0563B89F495AC3),
        C64(0x40E087931A00930D), C64(0x8CFFA9412EB642C1), C64(0x68CA39053261169F), C64(0x7A1EE967D27579E2),
        C64(0x9D1D60E5076F5B6F), C64(0x3810E399B6F65BA2), C64(0x32095B6D4AB5F9B1), C64(0x35CAB62109DD038A),
        C64(0xA90B24499FCFAFB1), C64(0x77A225A07CC2C6BD), C64(0x513E5E634C70E331), C64(0x4361C0CA3F692F12),
        C64(0xD941ACA44B20A45B), C64(0x528F7C8602C5807B), C64(0x52AB92BEB9613989), C64(0x9D1DFA2EFC557F73),
        C64(0x722FF175F572C348), C64(0x1D1260A51107FE97), C64(0x7A249A57EC0C9BA2), C64(0x04208FE9E8F7F2D6),
        C64(0x5A110C6058B920A0), C64(0x0CD9A497658A5698), C64(0x56FD23C8F9715A4C), C64(0x284C847B9D887AAE),
        C64(0x04FEABFBBDB619CB), C64(0x742E1E651C60BA83), C64(0x9A9632E65904AD3C), C64(0x881B82A13B51B9E2),
        C64(0x506E6744CD974924), C64(0xB0183DB56FFC6A79), C64(0x0ED9B915C66ED37E), C64(0x5E11E86D5873D484),
        C64(0xF678647E3519AC6E), C64(0x1B85D488D0F20CC5), C64(0xDAB9FE6525D89021), C64(0x0D151D86ADB73615),
        C64(0xA865A54EDCC0F019), C64(0x93C42566AEF98FFB), C64(0x99E7AFEABE000731), C64(0x48CBFF086DDF285A),
        C64(0x7F9B6AF1EBF78BAF), C64(0x58627E1A149BBA21), C64(0x2CD16E2ABD791E33), C64(0xD363EFF5F0977996),
        C64(0x0CE2A38C344A6EED), C64(0x1A804AADB9CFA741), C64(0x907F30421D78C5DE), C64(0x501F65EDB3034D07),
        C64(0x37624AE5A48FA6E9), C64(0x957BAF61700CFF4E), C64(0x3A6C27934E31188A), C64(0xD49503536ABCA345),
        C64(0x088E049589C432E0), C64(0xF943AEE7FEBF21B8), C64(0x6C3B8E3E336139D3), C64(0x364F6FFA464EE52E),
        C64(0xD60F6DCEDC314222), C64(0x56963B0DCA418FC0), C64(0x16F50EDF91E513AF), C64(0xEF1955914B609F93),
        C64(0x565601C0364E3228), C64(0xECB53939887E8175), C64(0xBAC7A9A18531294B), C64(0xB344C470397BBA52),
        C64(0x65D34954DAF3CEBD), C64(0xB4B81B3FA97511E2), C64(0xB422061193D6F6A7), C64(0x071582401C38434D),
        C64(0x7A13F18BBEDC4FF5), C64(0xBC4097B116C524D2), C64(0x59B97885E2F2EA28), C64(0x99170A5DC3115544),
        C64(0x6F423357E7C6A9F9), C64(0x325928EE6E6F8794), C64(0xD0E4366228B03343), C64(0x565C31F7DE89EA27),
        C64(0x30F5611484119414), C64(0xD873DB391292ED4F), C64(0x7BD94E1D8E17DEBC), C64(0xC7D9F16864A76E94),
        C64(0x947AE053EE56E63C), C64(0xC8C93882F9475F5F), C64(0x3A9BF55BA91F81CA), C64(0xD9A11FBB3D9808E4),
        C64(0x0FD22063EDC29FCA), C64(0xB3F256D8ACA0B0B9), C64(0xB03031A8B4516E84), C64(0x35DD37D5871448AF),
        C64(0xE9F6082B05542E4E), C64(0xEBFAFA33D7254B59), C64(0x9255ABB50D532280), C64(0xB9AB4CE57F2D34F3),
        C64(0x693501D628297551), C64(0xC62C58F97DD949BF), C64(0xCD454F8F19C5126A), C64(0xBBE83F4ECC2BDECB),
        C64(0xDC842B7E2819E230), C64(0xBA89142E007503B8), C64(0xA3BC941D0A5061CB), C64(0xE9F6760E32CD8021),
        C64(0x09C7E552BC76492F), C64(0x852F54934DA55CC9), C64(0x8107FCCF064FCF56), C64(0x098954D51FFF6580),
        C64(0x23B70EDB1955C4BF), C64(0xC330DE426430F69D), C64(0x4715ED43E8A45C0A), C64(0xA8D7E4DAB780A08D),
        C64(0x0572B974F03CE0BB), C64(0xB57D2E985E1419C7), C64(0xE8D9ECBE2CF3D73F), C64(0x2FE4B17170E59750),
        C64(0x11317BA87905E790), C64(0x7FBF21EC8A1F45EC), C64(0x1725CABFCB045B00), C64(0x964E915CD5E2B207),
        C64(0x3E2B8BCBF016D66D), C64(0xBE7444E39328A0AC), C64(0xF85B2B4FBCDE44B7), C64(0x49353FEA39BA63B1),
        C64(0x1DD01AAFCD53486A), C64(0x1FCA8A92FD719F85), C64(0xFC7C95D827357AFA), C64(0x18A6A990C8B35EBD),
        C64(0xCCCB7005C6B9C28D), C64(0x3BDBB92C43B17F26), C64(0xAA70B5B4F89695A2), C64(0xE94C39A54A98307F),
        C64(0xB7A0B174CFF6F36E), C64(0xD4DBA84729AF48AD), C64(0x2E18BC1AD9704A68), C64(0x2DE0966DAF2F8B1C),
        C64(0xB9C11D5B1E43A07E), C64(0x64972D68DEE33360), C64(0x94628D38D0C20584), C64(0xDBC0D2B6AB90A559),
        C64(0xD2733C4335C6A72F), C64(0x7E75D99D94A70F4D), C64(0x6CED1983376FA72B), C64(0x97FCAACBF030BC24),
        C64(0x7B77497B32503B12), C64(0x8547EDDFB81CCB94), C64(0x79999CDFF70902CB), C64(0xCFFE1939438E9B24),
        C64(0x829626E3892D95D7), C64(0x92FAE24291F2B3F1), C64(0x63E22C147B9C3403), C64(0xC678B6D860284A1C),
        C64(0x5873888850659AE7), C64(0x0981DCD296A8736D), C64(0x9F65789A6509A440), C64(0x9FF38FED72E9052F),
        C64(0xE479EE5B9930578C), C64(0xE7F28ECD2D49EECD), C64(0x56C074A581EA17FE), C64(0x5544F7D774B14AEF),
        C64(0x7B3F0195FC6F290F), C64(0x12153635B2C0CF57), C64(0x7F5126DBBA5E0CA7), C64(0x7A76956C3EAFB413),
        C64(0x3D5774A11D31AB39), C64(0x8A1B083821F40CB4), C64(0x7B4A38E32537DF62), C64(0x950113646D1D6E03),
        C64(0x4DA8979A0041E8A9), C64(0x3BC36E078F7515D7), C64(0x5D0A12F27AD310D1), C64(0x7F9D1A2E1EBE1327),
        C64(0xDA3A361B1C5157B1), C64(0xDCDD7D20903D0C25), C64(0x36833336D068F707), C64(0xCE68341F79893389),
        C64(0xAB9090168DD05F34), C64(0x43954B3252DC25E5), C64(0xB438C2B67F98E5E9), C64(0x10DCD78E3851A492),
        C64(0xDBC27AB5447822BF), C64(0x9B3CDB65F82CA382), C64(0xB67B7896167B4C84), C64(0xBFCED1B0048EAC50),
        C64(0xA9119B60369FFEBD), C64(0x1FFF7AC80904BF45), C64(0xAC12FB171817EEE7), C64(0xAF08DA9177DDA93D),
        C64(0x1B0CAB936E65C744), C64(0xB559EB1D04E5E932), C64(0xC37B45B3F8D6F2BA), C64(0xC3A9DC228CAAC9E9),
        C64(0xF3B8B6675A6507FF), C64(0x9FC477DE4ED681DA), C64(0x67378D8ECCEF96CB), C64(0x6DD856D94D259236),
        C64(0xA319CE15B0B4DB31), C64(0x073973751F12DD5E), C64(0x8A8E849EB32781A5), C64(0xE1925C71285279F5),
        C64(0x74C04BF1790C0EFE), C64(0x4DDA48153C94938A), C64(0x9D266D6A1CC0542C), C64(0x7440FB816508C4FE),
        C64(0x13328503DF48229F), C64(0xD6BF7BAEE43CAC40), C64(0x4838D65F6EF6748F), C64(0x1E152328F3318DEA),
        C64(0x8F8419A348F296BF), C64(0x72C8834A5957B511), C64(0xD7A023A73260B45C), C64(0x94EBC8ABCFB56DAE),
        C64(0x9FC10D0F989993E0), C64(0xDE68A2355B93CAE6), C64(0xA44CFE79AE538BBE), C64(0x9D1D84FCCE371425),
        C64(0x51D2B1AB2DDFB636), C64(0x2FD7E4B9E72CD38C), C64(0x65CA5B96B7552210), C64(0xDD69A0D8AB3B546D),
        C64(0x604D51B25FBF70E2), C64(0x73AA8A564FB7AC9E), C64(0x1A8C1E992B941148), C64(0xAAC40A2703D9BEA0),
        C64(0x764DBEAE7FA4F3A6), C64(0x1E99B96E70A9BE8B), C64(0x2C5E9DEB57EF4743), C64(0x3A938FEE32D29981),
        C64(0x26E6DB8FFDF5ADFE), C64(0x469356C504EC9F9D), C64(0xC8763C5B08D1908C), C64(0x3F6C6AF859D80055),
        C64(0x7F7CC39420A3A545), C64(0x9BFB227EBDF4C5CE), C64(0x89039D79D6FC5C5C), C64(0x8FE88B57305E2AB6),
        C64(0xA09E8C8C35AB96DE), C64(0xFA7E393983325753), C64(0xD6B6D0ECC617C699), C64(0xDFEA21EA9E7557E3),
        C64(0xB67C1FA481680AF8), C64(0xCA1E3785A9E724E5), C64(0x1CFC8BED0D681639), C64(0xD18D8549D140CAEA),
        C64(0x4ED0FE7E9DC91335), C64(0xE4DBF0634473F5D2), C64(0x1761F93A44D5AEFE), C64(0x53898E4C3910DA55),
        C64(0x734DE8181F6EC39A), C64(0x2680B122BAA28D97), C64(0x298AF231C85BAFAB), C64(0x7983EED3740847D5),
        C64(0x66C1A2A1A60CD889), C64(0x9E17E49642A3E4C1), C64(0xEDB454E7BADC0805), C64(0x50B704CAB602C329),
        C64(0x4CC317FB9CDDD023), C64(0x66B4835D9EAFEA22), C64(0x219B97E26FFC81BD), C64(0x261E4E4C0A333A9D),
        C64(0x1FE2CCA76517DB90), C64(0xD7504DFA8816EDBB), C64(0xB9571FA04DC089C8), C64(0x1DDC0325259B27DE),
        C64(0xCF3F4688801EB9AA), C64(0xF4F5D05C10CAB243), C64(0x38B6525C21A42B0E), C64(0x36F60E2BA4FA6800),
        C64(0xEB3593803173E0CE), C64(0x9C4CD6257C5A3603), C64(0xAF0C317D32ADAA8A), C64(0x258E5A80C7204C4B),
        C64(0x8B889D624D44885D), C64(0xF4D14597E660F855), C64(0xD4347F66EC8941C3), C64(0xE699ED85B0DFB40D),
        C64(0x2472F6207C2D0484), C64(0xC2A1E7B5B459AEB5), C64(0xAB4F6451CC1D45EC), C64(0x63767572AE3D6174),
        C64(0xA59E0BD101731A28), C64(0x116D0016CB948F09), C64(0x2CF9C8CA052F6E9F), C64(0x0B090A7560A968E3),
        C64(0xABEEDDB2DDE06FF1), C64(0x58EFC10B06A2068D), C64(0xC6E57A78FBD986E0), C64(0x2EAB8CA63CE802D7),
        C64(0x14A195640116F336), C64(0x7C0828DD624EC390), C64(0xD74BBE77E6116AC7), C64(0x804456AF10F5FB53),
        C64(0xEBE9EA2ADF4321C7), C64(0x03219A39EE587A30), C64(0x49787FEF17AF9924), C64(0xA1E9300CD8520548),
        C64(0x5B45E522E4B1B4EF), C64(0xB49C3B3995091A36), C64(0xD4490AD526F14431), C64(0x12A8F216AF9418C2),
        C64(0x001F837CC7350524), C64(0x1877B51E57A764D5), C64(0xA2853B80F17F58EE), C64(0x993E1DE72D36D310),
        C64(0xB3598080CE64A656), C64(0x252F59CF0D9F04BB), C64(0xD23C8E176D113600), C64(0x1BDA0492E7E4586E),
        C64(0x21E0BD5026C619BF), C64(0x3B097ADAF088F94E), C64(0x8D14DEDB30BE846E), C64(0xF95CFFA23AF5F6F4),
        C64(0x3871700761B3F743), C64(0xCA672B91E9E4FA16), C64(0x64C8E531BFF53B55), C64(0x241260ED4AD1E87D),
        C64(0x106C09B972D2E822), C64(0x7FBA195410E5CA30), C64(0x7884D9BC6CB569D8), C64(0x0647DFEDCD894A29),
        C64(0x63573FF03E224774), C64(0x4FC8E9560F91B123), C64(0x1DB956E450275779), C64(0xB8D91274B9E9D4FB),
        C64(0xA2EBEE47E2FBFCE1), C64(0xD9F1F30CCD97FB09), C64(0xEFED53D75FD64E6B), C64(0x2E6D02C36017F67F),
        C64(0xA9AA4D20DB084E9B), C64(0xB64BE8D8B25396C1), C64(0x70CB6AF7C2D5BCF0), C64(0x98F076A4F7A2322E),
        C64(0xBF84470805E69B5F), C64(0x94C3251F06F90CF3), C64(0x3E003E616A6591E9), C64(0xB925A6CD0421AFF3),
        C64(0x61BDD1307C66E300), C64(0xBF8D5108E27E0D48), C64(0x240AB57A8B888B20), C64(0xFC87614BAF287E07),
        C64(0xEF02CDD06FFDB432), C64(0xA1082C0466DF6C0A), C64(0x8215E577001332C8), C64(0xD39BB9C3A48DB6CF),
        C64(0x2738259634305C14), C64(0x61CF4F94C97DF93D), C64(0x1B6BACA2AE4E125B), C64(0x758F450C88572E0B),
        C64(0x959F587D507A8359), C64(0xB063E962E045F54D), C64(0x60E8ED72C0DFF5D1), C64(0x7B64978555326F9F),
        C64(0xFD080D236DA814BA), C64(0x8C90FD9B083F4558), C64(0x106F72FE81E2C590), C64(0x7976033A39F7D952),
        C64(0xA4EC0132764CA04B), C64(0x733EA705FAE4FA77), C64(0xB4D8F77BC3E56167), C64(0x9E21F4F903B33FD9),
        C64(0x9D765E419FB69F6D), C64(0xD30C088BA61EA5EF), C64(0x5D94337FBFAF7F5B), C64(0x1A4E4822EB4D7A59),
        C64(0x6FFE73E81B637FB3), C64(0xDDF957BC36D8B9CA), C64(0x64D0E29EEA8838B3), C64(0x08DD9BDFD96B9F63),
        C64(0x087E79E5A57D1D13), C64(0xE328E230E3E2B3FB), C64(0x1C2559E30F0946BE), C64(0x720BF5F26F4D2EAA),
        C64(0xB0774D261CC609DB), C64(0x443F64EC5A371195), C64(0x4112CF68649A260E), C64(0xD813F2FAB7F5C5CA),
        C64(0x660D3257380841EE), C64(0x59AC2C7873F910A3), C64(0xE846963877671A17), C64(0x93B633ABFA3469F8),
        C64(0xC0C0F5A60EF4CDCF), C64(0xCAF21ECD4377B28C), C64(0x57277707199B8175), C64(0x506C11B9D90E8B1D),
        C64(0xD83CC2687A19255F), C64(0x4A29C6465A314CD1), C64(0xED2DF21216235097), C64(0xB5635C95FF7296E2),
        C64(0x22AF003AB672E811), C64(0x52E762596BF68235), C64(0x9AEBA33AC6ECC6B0), C64(0x944F6DE09134DFB6),
        C64(0x6C47BEC883A7DE39), C64(0x6AD047C430A12104), C64(0xA5B1CFDBA0AB4067), C64(0x7C45D833AFF07862),
        C64(0x5092EF950A16DA0B), C64(0x9338E69C052B8E7B), C64(0x455A4B4CFE30E3F5), C64(0x6B02E63195AD0CF8),
        C64(0x6B17B224BAD6BF27), C64(0xD1E0CCD25BB9C169), C64(0xDE0C89A556B9AE70), C64(0x50065E535A213CF6),
        C64(0x9C1169FA2777B874), C64(0x78EDEFD694AF1EED), C64(0x6DC93D9526A50E68), C64(0xEE97F453F06791ED),
        C64(0x32AB0EDB696703D3), C64(0x3A6853C7E70757A7), C64(0x31865CED6120F37D), C64(0x67FEF95D92607890),
        C64(0x1F2B1D1F15F6DC9C), C64(0xB69E38A8965C6B65), C64(0xAA9119FF184CCCF4), C64(0xF43C732873F24C13),
        C64(0xFB4A3D794A9A80D2), C64(0x3550C2321FD6109C), C64(0x371F77E76BB8417E), C64(0x6BFA9AAE5EC05779),
        C64(0xCD04F3FF001A4778), C64(0xE3273522064480CA), C64(0x9F91508BFFCFC14A), C64(0x049A7F41061A9E60),
        C64(0xFCB6BE43A9F2FE9B), C64(0x08DE8A1C7797DA9B), C64(0x8F9887E6078735A1), C64(0xB5B4071DBFC73A66),
        C64(0x230E343DFBA08D33), C64(0x43ED7F5A0FAE657D), C64(0x3A88A0FBBCB05C63), C64(0x21874B8B4D2DBC4F),
        C64(0x1BDEA12E35F6A8C9), C64(0x53C065C6C8E63528), C64(0xE34A1D250E7A8D6B), C64(0xD6B04D3B7651DD7E),
        C64(0x5E90277E7CB39E2D), C64(0x2C046F22062DC67D), C64(0xB10BB459132D0A26), C64(0x3FA9DDFB67E2F199),
        C64(0x0E09B88E1914F7AF), C64(0x10E8B35AF3EEAB37), C64(0x9EEDECA8E272B933), C64(0xD4C718BC4AE8AE5F),
        C64(0x81536D601170FC20), C64(0x91B534F885818A06), C64(0xEC8177F83F900978), C64(0x190E714FADA5156E),
        C64(0xB592BF39B0364963), C64(0x89C350C893AE7DC1), C64(0xAC042E70F8B383F2), C64(0xB49B52E587A1EE60),
        C64(0xFB152FE3FF26DA89), C64(0x3E666E6F69AE2C15), C64(0x3B544EBE544C19F9), C64(0xE805A1E290CF2456),
        C64(0x24B33C9D7ED25117), C64(0xE74733427B72F0C1), C64(0x0A804D18B7097475), C64(0x57E3306D881EDB4F),
        C64(0x4AE7D6A36EB5DBCB), C64(0x2D8D5432157064C8), C64(0xD1E649DE1E7F268B), C64(0x8A328A1CEDFE552C),
        C64(0x07A3AEC79624C7DA), C64(0x84547DDC3E203C94), C64(0x990A98FD5071D263), C64(0x1A4FF12616EEFC89),
        C64(0xF6F7FD1431714200), C64(0x30C05B1BA332F41C), C64(0x8D2636B81555A786), C64(0x46C9FEB55D120902),
        C64(0xCCEC0A73B49C9921), C64(0x4E9D2827355FC492), C64(0x19EBB029435DCB0F), C64(0x4659D2B743848A2C),
        C64(0x963EF2C96B33BE31), C64(0x74F85198B05A2E7D), C64(0x5A0F544DD2B1FB18), C64(0x03727073C2E134B1),
        C64(0xC7F6AA2DE59AEA61), C64(0x352787BAA0D7C22F), C64(0x9853EAB63B5E0B35), C64(0xABBDCDD7ED5C0860),
        C64(0xCF05DAF5AC8D77B0), C64(0x49CAD48CEBF4A71E), C64(0x7A4C10EC2158C4A6), C64(0xD9E92AA246BF719E),
        C64(0x13AE978D09FE5557), C64(0x730499AF921549FF), C64(0x4E4B705B92903BA4), C64(0xFF577222C14F0A3A),
        C64(0x55B6344CF97AAFAE), C64(0xB862225B055B6960), C64(0xCAC09AFBDDD2CDB4), C64(0xDAF8E9829FE96B5F),
        C64(0xB5FDFC5D3132C498), C64(0x310CB380DB6F7503), C64(0xE87FBB46217A360E), C64(0x2102AE466EBB1148),
        C64(0xF8549E1A3AA5E00D), C64(0x07A69AFDCC42261A), C64(0xC4C118BFE78FEAAE), C64(0xF9F4892ED96BD438),
        C64(0x1AF3DBE25D8F45DA), C64(0xF5B4B0B0D2DEEEB4), C64(0x962ACEEFA82E1C84), C64(0x046E3ECAAF453CE9),
        C64(0xF05D129681949A4C), C64(0x964781CE734B3C84), C64(0x9C2ED44081CE5FBD), C64(0x522E23F3925E319E),
        C64(0x177E00F9FC32F791), C64(0x2BC60A63A6F3B3F2), C64(0x222BBFAE61725606), C64(0x486289DDCC3D6780),
        C64(0x7DC7785B8EFDFC80), C64(0x8AF38731C02BA980), C64(0x1FAB64EA29A2DDF7), C64(0xE4D9429322CD065A),
        C64(0x9DA058C67844F20C), C64(0x24C0E332B70019B0), C64(0x233003B5A6CFE6AD), C64(0xD586BD01C5C217F6),
        C64(0x5E5637885F29BC2B), C64(0x7EBA726D8C94094B), C64(0x0A56A5F0BFE39272), C64(0xD79476A84EE20D06),
        C64(0x9E4C1269BAA4BF37), C64(0x17EFEE45B0DEE640), C64(0x1D95B0A5FCF90BC6), C64(0x93CBE0B699C2585D),
        C64(0x65FA4F227A2B6D79), C64(0xD5F9E858292504D5), C64(0xC2B5A03F71471A6F), C64(0x59300222B4561E00),
        C64(0xCE2F8642CA0712DC), C64(0x7CA9723FBB2E8988), C64(0x2785338347F2BA08), C64(0xC61BB3A141E50E8C),
        C64(0x150F361DAB9DEC26), C64(0x9F6A419D382595F4), C64(0x64A53DC924FE7AC9), C64(0x142DE49FFF7A7C3D),
        C64(0x0C335248857FA9E7), C64(0x0A9C32D5EAE45305), C64(0xE6C42178C4BBB92E), C64(0x71F1CE2490D20B07),
        C64(0xF1BCC3D275AFE51A), C64(0xE728E8C83C334074), C64(0x96FBF83A12884624), C64(0x81A1549FD6573DA5),
        C64(0x5FA7867CAF35E149), C64(0x56986E2EF3ED091B), C64(0x917F1DD5F8886C61), C64(0xD20D8C88C8FFE65F),
        C64(0x31D71DCE64B2C310), C64(0xF165B587DF898190), C64(0xA57E6339DD2CF3A0), C64(0x1EF6E6DBB1961EC9),
        C64(0x70CC73D90BC26E24), C64(0xE21A6B35DF0C3AD7), C64(0x003A93D8B2806962), C64(0x1C99DED33CB890A1),
        C64(0xCF3145DE0ADD4289), C64(0xD0E4427A5514FB72), C64(0x77C621CC9FB3A483), C64(0x67A34DAC4356550B),
        C64(0xF8D626AAAF278509)
    };

    // Indices to the Random64[] array
    const int CASTLE_OFFSET = 768;
    const int EP_OFFSET = 772;
    const int STM_OFFSET = 780; //site to move

    // Local functions

}

int TBook::findMoves(TBoard * pos, TMoveList * list) {
    list->clear();

    if (!is_open() || this->bookSize == 0) {
        return 0;
    }
    TBookEntry entry;
    U64 key = this->polyglot_key(pos);
    for (int idx = find_key(key); idx < this->bookSize; idx++) {
        read_entry(entry, idx);
        if (entry.key != key) {
            break;
        }
        TMove * move = list->last++;
        readPolyglotMove(pos, move, entry.move);
        move->score = entry.weight;
    }
    return list->last - list->first;
}

void TBook::readPolyglotMove(TBoard* pos, TMove * move, int polyglotMove) {
    int tsq = polyglotMove & 63;
    int ssq = (polyglotMove >> 6) & 63;
    int piece = pos->matrix[ssq];

    move->setMove(piece, ssq, tsq);
    move->capture = pos->matrix[tsq];
    move->castle = 0;
    move->en_passant = false;
    if (pos->stack->enpassant_sq && tsq == pos->stack->enpassant_sq) {
        if (piece == WPAWN) {
            move->capture = BPAWN;
            move->en_passant = true;
        } else if (piece == BPAWN) {
            move->capture = WPAWN;
            move->en_passant = true;
        }
    }

    //"normal" moves
    int promotion = (polyglotMove >> 12) & 7; //none 0, knight 1, bishop 2, rook 3, queen 4
    if (promotion) {
        promotion += pos->stack->wtm ? 1 : 7; //WKNIGHT == 2; BKNIGHT == 8;
        move->promotion = promotion;
    } else if (piece == BKING && ssq == e8) {
        if (tsq == h8) {
            move->tsq = g8;
            move->castle = CASTLE_k;
            move->capture = 0;
        } else if (tsq == a8) {
            move->tsq = c8;
            move->castle = CASTLE_q;
            move->capture = 0;
        }
    } else if (piece == WKING && ssq == e1) {
        if (tsq == h1) {
            move->tsq = g1;
            move->castle = CASTLE_K;
            move->capture = 0;
        } else if (tsq == a1) {
            move->tsq = c1;
            move->castle = CASTLE_Q;
            move->capture = 0;
        }
    }
}

void TBook::close() {
    if (is_open())
        ifstream::close();
}

void TBook::open(const string& fName) {

    // Close old file before opening the new
    close();

    fileName = fName;
    ifstream::open(fileName.c_str(), ifstream::in | ifstream::binary);

    // Silently return when asked to open a non-exsistent file
    if (!is_open()) {
        return;
    }

    // Get the book size in number of entries
    seekg(0, ios::end);
    bookSize = long(tellg()) / EntrySize;
    seekg(0, ios::beg);

    if (!good()) {
        exit(EXIT_FAILURE);
    }
}


/// Book::file_name() returns the file name of the currently active book,
/// or the empty string if no book is open.

const string TBook::file_name() {
    return is_open() ? fileName : "";
}



/// Book::find_key() takes a book key as input, and does a binary search
/// through the book file for the given key. The index to the first book
/// entry with the same key as the input is returned. When the key is not
/// found in the book file, bookSize is returned.

int TBook::find_key(U64 key) {

    int left, right, mid;
    TBookEntry entry;

    // Binary search (finds the leftmost entry)
    left = 0;
    right = bookSize - 1;

    assert(left <= right);

    while (left < right) {
        mid = (left + right) / 2;

        assert(mid >= left && mid < right);

        read_entry(entry, mid);

        if (key <= entry.key)
            right = mid;
        else
            left = mid + 1;
    }

    assert(left == right);

    read_entry(entry, left);
    return entry.key == key ? left : bookSize;
}


/// Book::read_entry() takes a BookEntry reference and an integer index as
/// input, and looks up the opening book entry at the given index in the book
/// file. The book entry is copied to the first input parameter.

void TBook::read_entry(TBookEntry& entry, int idx) {

    assert(idx >= 0 && idx < bookSize);
    assert(is_open());

    seekg(idx * EntrySize, ios_base::beg);

    *this >> entry;

    if (!good()) {
        exit(EXIT_FAILURE);
    }
}


/// Book::read_integer() reads size chars from the file stream
/// and converts them in an integer number.

U64 TBook::read_integer(int size) {

    char buf[8];
    U64 n = 0;

    read(buf, size);

    // Numbers are stored on disk as a binary byte stream
    for (int i = 0; i < size; i++) {
        n = (n << 8) + (unsigned char) buf[i];
    }

    return n;
}

U64 TBook::polyglot_key(TBoard* pos) {
    static const int PolyGlotPiece[] = {0, 1, 3, 5, 7, 9, 11, 0, 2, 4, 6, 8, 10};

    U64 result = 0;
    U64 occupied = pos->all_pieces;

    while (occupied) {
        int sq = popFirst(occupied);
        result ^= Random64[PolyGlotPiece[pos->matrix[sq]]*64 + sq];
    }

    if (pos->castleRight(CASTLE_K)) {
        result ^= Random64[CASTLE_OFFSET + 0];
    }
    if (pos->castleRight(CASTLE_Q)) {
        result ^= Random64[CASTLE_OFFSET + 1];
    }
    if (pos->castleRight(CASTLE_k)) {
        result ^= Random64[CASTLE_OFFSET + 2];
    }
    if (pos->castleRight(CASTLE_q)) {
        result ^= Random64[CASTLE_OFFSET + 3];
    }

    if (pos->stack->enpassant_sq >= a3 //polyglot only considers en passant if ep captures are possible
            && ((pos->stack->wtm && (BPAWN_CAPTURES[pos->stack->enpassant_sq] & pos->white_pawns))
            || (pos->stack->wtm == false && (WPAWN_CAPTURES[pos->stack->enpassant_sq] & pos->black_pawns)))) {
        result ^= Random64[EP_OFFSET + FILE(pos->stack->enpassant_sq)];

    }
    result ^= pos->stack->wtm ? Random64[STM_OFFSET] : 0;
    return result;
}










