// 用真实抓包向量校验 MagicCrypto 的 C++ 实现与已验证的离线参照一致。
// 所有期望值都取自真实 app 数据 (D:\project\ida\dm)，不是自造样例。
#include "MagicCrypto.h"

#include <QByteArray>
#include <QString>
#include <cstdio>

namespace {
constexpr qint64 kSlot = 497227;  // 1790018780 / 3600
const char kSecret[] =
    "61333865323761643764383332323166636139326361313462383164316366643764343135326533333936663362643766653630656335"
    "303362303734323337";
const char kWireMask[] =
    "038db53236befda87e84c4c47837d9df08eda3d310b36e3ca92ea7af4f0dca2c";
const char kTag1[] =
    "69bb4dce2e2c4cffdc3230171b0c6719f12ede5a442f679da5983a44a2cc3396";
const char kEnv1[] =
    "0300000000000007964b040399b68e006e0010a7cc065a352bc5cf5a940d23ba06e44e002ca6601bae49dc21c95e429e85c5a7eef3b71b"
    "9cbd3de94348ef02a210d0d2eccb9035ded708661f69a6f86d48002cec6175a6f65573773b7659f132f7529fc4b8c9843dc6478c864eb3"
    "179e2e3fbd17284e8940219130a70dd18945d7357b558f1f4e029fdb918f254f1055f916a00508c4f197f4e62312d05277a218cfaa1f3e"
    "a2251876887cc23b26b00de12234c23294b5baded46df176887c3777ff01b4483a4e7ffacb8c4b1a53c002f37507a4d25dc650f1dd4e2c"
    "671d8769bb4dce2e2c4cffdc3230171b0c6719f12ede5a442f679da5983a44a2cc3396";
const char kPlain1[] =
    "7b22766964656f4964223a22333138313836363937363334373338323035222c22657069736f64654e756d223a312c2270616765496e64"
    "6578223a312c227061676553697a65223a31302c22736f72744279223a22686f74227d";
const char kLiveEnv[] =
    "0300000000000007965504031a73f5002600000010a66e9c7f5f9045eeede8e70d3e5eb0b5001008bb0814813b0fa834026f04445ac36c"
    "0b1e3575ff886d67b2aa9294abf2ec38cfa2a57f6b8812d99e8dfaab686117d096d8e903e4d9877eed0a1d3279101464664207da1941ec"
    "e960c757cbaa8642f9ef83d18da4273cba9aea24c0fabad3657f5f08d0bc4e9323b41e0967a99649b3d524a62805462083e4dfaa3b4df7"
    "6007c2aff1006fb015caed1833d8a245911dbf44e2145c8cd7067d76b16cf6dcb8aa99677b3c1097f9ac40e840e729ac59198b586876fe"
    "55f95fad9cb1880c71fdf4e1503451074689810228b07149dafff1b6309df8847f3d61bcb591680c06b25b456007796fd6c4ce8b06391d"
    "68fead7c5339ab421150595b220cbc7e4490447302e8c0d07bd2c892f169c4f638c6459b5c71b595aaa2fab0fb4c52be3157bf750397f1"
    "ed235f9bafa88365eb18943ae7f25c75cd6affa37641e9f107761fc6e600b3286af7a0aeda88b279b26f93a5a2009adb4c47a22748dc80"
    "e4b9faf6121d60f87d8b9f1f8226af30b72f7d12f67b620c84f3addf7bacbb204ad4fa48e920f25c75cd6affa376280da5f849b3949bad"
    "43e50f4e81c5c242c21807017c2f50a277e287d521a3d88a975d99df2b336709ed6762b7e429781ec6955abb9d2aa79f26867ef2b81a4e"
    "29184dba1e24ac0c472218066bd50d2820de1ae13b73a3bdb64de448fbd647e83bed329bf14a474a317b27df701be1194108db680634ff"
    "59c37ca249acca71f013c8f38f33089ddb1946385a6cb0a156d57c4641182f91c8e275b5b282493a926d2a1ba6939925cd5bee4161288c"
    "721de2f04e6cd68742610033361cecb9c4f57f6d171d245910bae3acdfb11960550c0a3c2473a06e0e00e9d38b424dcd2cbb2b3429cdeb"
    "3726a460fee0475e8df290f5340150674ab0aeb001e96503b59af4dea2bab3013fbd23ab80424b9bdd67b709c1afac7a61b531fddb8739"
    "46e2c14979f494efe51f464e830b2f47b21ac8c816665010caa2c832d43ff547fd10e95e5bd802dfd9d03a81787fb2c19c8a64dd10f6b7"
    "1cdb67781db7724557d13629279f5a46e4ec3c01ee630fe60150acb88a07c51352d80e5f320f22cacfcd738ec9ce0147ca8211b4912beb"
    "b5f2884347342cf37aa931197f3aef4c7242a119ca0cea42120761e65b4c54c6757a09cfdde72333c2fdfb2ba00b1ff3b64308c9240e35"
    "484d60e20fbf24ce4e17c3cee0855bc08ba5747feba420054547da077517ef0ca7d2d3196b1c2d4214bc8b2dbff7519862e87534d6b6ac"
    "0dd5fd457ebd18166235b1e5a84c1e0b653bb9714f570d3e77276bd4b040d2fb77fc68fbe5620bbe4dcb56f6014fffbbc67533f57b86f3"
    "dd68a8ec6ca06a42d8a7273c45532d67c100f15c385aae578946e74642dce44a7d52d1314134dad22f23c1aecf60f2fb082dc83909d3e0"
    "7c89e3f4342a6743cab134fed6c21f1ff5f58c6f57c84def91b55a466e9863bbe4b65e95bc974593b5ac17cd3c54be6a0280813152fb01"
    "9521bce52e8d7c564c35e7b5261abde77bf5f42eee073549c66023a815b038c29166abfafdcbf77df445b8b2766a8d75f825c439f4fe01"
    "a352b5fc1670c8858117269bed54be78674dbbe77474a41e75a9b2aae18563d3357ec5878cdf42f8209aea9c042be778beac08724ba8e0"
    "a965f2b6a1319572800673ebe0156776cea3bfb622a5a603032bbc5cc7923d5413336653ad2c501849732bf7570fdef6976ca71dc8eda9"
    "e28c5715e07d7a4eed9ed178892d2af777f3fc1bffcdc643aaf08528c114b7159cae2d39700e5ade2480f367dd10553c8f5defcde7e264"
    "02b4e7f00028187474a41e75a9b2aae0a1891b1279cd64b665cbfd2f38b69c7fc70b7f9a551cf6493dc1c3c4e63b50bc86ae002378ef76"
    "2b507e3dd96ad2abc958e52ef9c7103fe9dd037d1ba9c020c30485c06a1c475e49e9165d80d4206e223c9b658062199375f27e7501a3db"
    "995bd216a8b3fd2f2311896a410b5fbdb2f77f584bd16c45f86c137dc5a187b0190fbb9da5432cf33ed461f226bf0071f6640f3be688fc"
    "d842c669ce1708e295b252531674b730a3cecc142df955f6c95bafb7d2d8f03d01f7e44b7cfe884ee57f13baf144d3d7de1dd8fde37ec0"
    "8fdb2a1b5b8b08889860f115ff7b2d00297410b29a679fde870a85475a6638b89ecf58a3437c1f0be39457ef7c9802a7fb8d1377b387df"
    "65c11de0d8ea139884f909a74e3d574bc0dc0c95432c5c38da741c507e5ac4208b35ffadb45813e16dfec0c217b0a8ac89e3cb84f01fce"
    "d1421bd0b7e32d8e1620d2bbf3c27f58c8897360d879bd5e5d3309df35bf1913b01d556075a0e69507d5b3492504947800529f1e2085bd"
    "d004cf941534d5c48694c327bf3931c3fd4311134c4f39eef511557c820250c8c4508f2520669bbfec324f2d933339eef511557c820242"
    "c4e26a855e45e86587944ec583f7bf61b2e52eda4580cbc6f2308568c9cd2c857c1b7cf9bd9ffae2c34bb8c526137941b6a5eeb4a325d9"
    "7d5c11564532a348573aa94a1892f5b774b5ac7459cdeb2d9bc534eec42e456cc7ff60d611a44a4bbc0356d415023ffc146b84fbb89af0"
    "6194782cbf928df5aed40704fa04f329c73823a99a576e6e19d7882ec908c141ce574ebb585b5b27db949f785641169a90a99a401a80f5"
    "4b358df35e2467fdd3bbaa9db36c30c8084d19d266994862fdc9a6b9893ce96ad3f6fd392829f716f6327a3736cc00406d0c9446c3c142"
    "649bffee510fa4d893ac6a1851bbc59b6450d1f7e64ce3a8badb73b50c8e2c359637a8db4ff1de8a087f84682430eda2b4cad6b73c7593"
    "f1d8108b98efafbb52a54415d0e394d84f0daa90f2ecac760a0bacc1f774d7bd86af5db03fb013acac7336d26f0bdde65f1c7c1193b7a5"
    "92cbdc2c191946385a6cb0a1568d253e02f748187410f931d3abd11c789768bd77c90f3f019f2a7bdcd17c8d8cf93a2d37996c79c1f46e"
    "fc52f6414a4dcaea9b00cb3ac4f7d48dc70deb53e30093b7a592cbdc2c191946385a6cb0a1561bdf1be3428ef199b148ea11fbf1b97294"
    "cf96c4438afe0f54628df2cff7ef19aa34d083a93fdef412200820c8de7ff52ef5f162e8bb47c3a4e6e6436a69ae9eddf84226e3aca6a5"
    "027eedee7f6b34c22b5588164cfd5613124de1836a1aff6c2cc6083eb292ade473cd5ee8f09a7c87cbfe4a3fad57bd1b39ae8adf4ee7e9"
    "2a334ca9ed5860c726a984b28bc84b79f5ea7b930876a9c47389d5e71446d54346c0e042f9dba54749d55f5ebaf02e45129840cb75ba04"
    "6dccaeedf42b1ecc38318c044fa7fdda65477ecf773b6b5dbd4f4a021a795f8c7dc8975813783813530fec8a4472890abd0665c5181067"
    "97b904c803ce8c6a20adb9554215193e8a60380caf609939faabe898ea6e6872decb10ec26f84278e55a20c73be5cb22fd004d4efbe0dc"
    "5719fe9fc07ac50bd7c5875b7b570cf4a60b2d85f4b536668f162f8b9dc536a5678793d0e995932a344cfa7b2f9230f97a1a35d9d9fe9d"
    "7bda0fbc1828b17ce3d427122478570b02f2a090e977ff0caab114165a50efd8d50c716065ad74f7f12e938c7992738a6901cefb723828"
    "b4d2b034409b17bb586f65fede872e142de64031420799c23d2b7da08611ea293b638d7b368e3fa6ccc92abb6c8c36f2567c1d4af9372a"
    "c2581b44514270";
const char kBootJpeg[] =
    "/9j/4AAQSkZJRgABAQABLAEsAAD/4QCARXhpZgAATU0AKgAAAAgABAEaAAUAAAABAAAAPgEbAAUAAAABAAAARgEoAAMAAAABAAIAAIdpAAQAAA"
    "ABAAAATgAAAAAAAAEsAAAAAQAAASwAAAABAAOgAQADAAAAAQABAACgAgAEAAAAAQAAAQCgAwAEAAAAAQAAAQAAAAAA/+0AOFBob3Rvc2hvcCAz"
    "LjAAOEJJTQQEAAAAAAAAOEJJTQQlAAAAAAAQ1B2M2Y8AsgTpgAmY7PhCfv/AABEIAQABAAMBIgACEQEDEQH/xAAfAAABBQEBAQEBAQAAAAAAAA"
    "AAAQIDBAUGBwgJCgv/xAC1EAACAQMDAgQDBQUEBAAAAX0BAgMABBEFEiExQQYTUWEHInEUMoGRoQgjQrHBFVLR8CQzYnKCCQoWFxgZGiUmJygp"
    "KjQ1Njc4OTpDREVGR0hJSlNUVVZXWFlaY2RlZmdoaWpzdHV2d3h5eoOEhYaHiImKkpOUlZaXmJmaoqOkpaanqKmqsrO0tba3uLm6wsPExcbHyM"
    "nK0tPU1dbX2Nna4eLj5OXm5+jp6vHy8/T19vf4+fr/xAAfAQADAQEBAQEBAQEBAAAAAAAAAQIDBAUGBwgJCgv/xAC1EQACAQIEBAMEBwUEBAAB"
    "AncAAQIDEQQFITEGEkFRB2FxEyIygQgUQpGhscEJIzNS8BVictEKFiQ04SXxFxgZGiYnKCkqNTY3ODk6Q0RFRkdISUpTVFVWV1hZWmNkZWZnaG"
    "lqc3R1dnd4eXqCg4SFhoeIiYqSk5SVlpeYmZqio6Slpqeoqaqys7S1tre4ubrCw8TFxsfIycrS09TV1tfY2dri4+Tl5ufo6ery8/T19vf4+fr/"
    "2wBDAAYGBgYGBgoGBgoOCgoKDhIODg4OEhcSEhISEhccFxcXFxcXHBwcHBwcHBwiIiIiIiInJycnJywsLCwsLCwsLCz/2wBDAQcHBwsKCxMKCh"
    "MuHxofLi4uLi4uLi4uLi4uLi4uLi4uLi4uLi4uLi4uLi4uLi4uLi4uLi4uLi4uLi4uLi4uLi7/3QAEABD/2gAMAwEAAhEDEQA/AO0ooor6Q+dF"
    "pKKKLALRSUUrAFFFFMApaSiiwC0UlFFgFpKKKLALRSUUrAFFFFMBaKSlpWAKSiiiwC0UlFFgFopKKLAFFFFMAooooAWikoosB//Q7SiiivpT50"
    "KKKKBhRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFIAooopgFFFFABRRRQAUUUUCCiiigD/0e0ooor6Q+eCiiigAooopgFFFFAB"
    "RRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUUUAFFFFAH/9LtKKKK+lPngooooAKKKKACiiigAooooAKKKKACiiigAo"
    "oooAKKKKACiiigAooooAKKKKACiiigAooooAKKKKACiiigD//T7SlxRRX0dz5+wYoxRRRcLBikxS0UXCwYoxRRRcLBijFFFFwsGKMUUUXCwYox"
    "RRRcLBijFFFFwsGKMUUUXCwYoxRRRcLBijFFFFwsGKMUUUXCwYoxRRRcLBijFFFFwsGKMUUUXCwYoxRRRcLH/9TtqMU6ivojw7CUYpaKAsJRS0"
    "UBYTFJinUUBYSkp1FAWG4pcUtFAWG4oxTqKAsJiilooCwmKKWigLCYpMU6igLDcUtLRQFhtFOooCw3FGKdRQFhuKKdRQFhuKMU6igLDcUYp1FA"
    "WP/V7milxRX0J49hKKXFGKA5RKKXFGKA5RKKXFGKLhyiUUuKKLhyiUUuKMUXDlEopcUUXDlEopaMUXDlEopcUYouHKJRS0UXDlEopaKLhyiUUt"
    "FFw5RKKWigOUSilxRii4colFLiii4colGKXFFFw5T/1u7oxRijFe/c8ywUUYoxQFgoooxRcLBRRiii4WCijFGKLhYKKMUYouFgooxRii4WCijF"
    "GKLhYKKMUYouFgooxRii4WCijFGKLhYKKMUYouFgooxRii4WCijFGKLhYKKMUYouFgoooouFj//X72iiiveOGwUUUYouFgooxRRcLBRRRRcLBR"
    "RRQFgoooouFgoxRRRcLBRRRRcLBRxRRigLBRRRRcLBRRRRcLBRRRRcLBRRRRcLBRRijFFwsFFGKMUXCwUUYooCx//Q76iiivcOblCiiigOUKKK"
    "WgOUSilpKA5QopaSgOUKKWigOUSilooDlEopaKAsJRRS0ByiUUUtAcolFLSUByhRRRQHKFFLRQHKJRS0UByiUUtFAWEopaSgOU//0e/ooor27m"
    "dgxRilopXCwlFLRRcLCUUtFFwsJRiloouFhMUYpaKLhYKMUUUXCwmKWiii4WExRiloouFhMUYpaKLhYSjFLRRcLCYoxS0UXCwlFLRRcLCUUtFF"
    "wsJRS0UXCwlFLSUXCx//0vQKKKK9m4WCkp1FFwsJSZp1JRcLCUtGKWgLCUUtFFwsJRS0UBYSjNLRRcLDc0uaWii4WG5op1FFwsJmiloouFhM0U"
    "tFFwsJRS0UXCw3NLS0UBYbminUUXCw3NLmloouFhuaWloouFj/0/QKWikr2CwzRS0UDsGaM0UUrhYSilop3CwUZooouFgzRRRSuFgzRmiii4WE"
    "zRmlop3CwlLRRSuIKM0UUXHYM0UUUXCwUUUUXCwUUUUCCiiii47Bmiiii4WCiiincLH/1PQKKKK9g0FooopAFFFFABRRRQAUUUUAFFJS0AFFFF"
    "ABRRRQAUUUUCCiiigYUUUUAFFFFABRRRQAUUUUAFFFFABRRSUAf//V9ApaKK9c0CiiigAooooAKKSloAKKSloAKKKKACiiigYUUUUCCiiigQUU"
    "UUDCiiigAooooCwUUUUAFFFFABRRRQAUlLSUAf/W9BoozSV65oLmikpaACiiigAzRRSUALRRRQAUUUUAFFFFABRRRQAUUUUAFFFFABRRRQAUUU"
    "UAFFGaKACiiigAooooAKSlpKAP/9mJUENHAAABsD3tZelK+A72nyviUJTWIMmSWTDvVkSXpfdWTbgjwVhc8/CfESzWL+JO6NVLuLpcf3gf2yUX"
    "iO7GHRUefM5oOLthgSEEAjDnLRDgsTfRc9+w7cRkDjygfxzqmOaNjIYvNN/z0mz0xNcsYwmTdtkpkdp2wHmZeQTNtG72Y/UrnXbIBa2y5v3XoB"
    "pPnDO3IUWVdC5m7FpJFH/keFswxnRF3awJdrC5aS4kHYlqErnTSzaxEQQUn5qjkIVSoA7egpWCBJBdQRdMeOg52jdGlS2H6bMUzWXMmdtKCYcP"
    "FB5lEk7y7YDvUun9jMKGS2lqlM6HWWBb/jxLOj2OiBx+0jcid7OSrFR5PULpj6iddWwyfdAtH4mmO6D6qA9StXvAST+LUi74omVS4F8k8Yu4xW"
    "bDDBH3YgxIv2Ms3uZKb8uR3vk1lUVKNZpnotSNu7RxxEG3kW3ICN7ysQO9qG0H0WlUhq+qe4Tb6yWSHxcLXpnYxVJ6rettNp5+s/JfHe3vYnOw"
    "bIX2Wm9TC3aTip9qHj3gQSazKrbWjzl+qbDCoBL1bzRb/AxznusAcVgMNwfzLUUZ1Wb2HWcfMpzGns+CfCg+7pIcAl+UQXYUOBKu/ycQTvi0Bi"
    "rWLOtCX0ygwC0WyDZUbQ==";

const char kAppId[] = "315891530526580736";
const char kDeviceId[] = "4784d62e3c6c05a96cb35fa184b14ad168c5896c2f6128b4d8c422319bc7cc5c";
const char kBootMasterSecret[] = "7b94a2ab809388c89cdee6751701e638";
const char kBootSignSecret[] = "b7a599975b90ac560466466fa8a3021f";

int g_fail = 0;
void check(bool ok, const char *what) {
    std::printf("%-56s %s\n", what, ok ? "ok" : "** FAIL **");
    if (!ok) ++g_fail;
}
}  // namespace

int main() {
    const QByteArray secret = QByteArray::fromHex(kSecret);
    const QByteArray env1 = QByteArray::fromHex(kEnv1);
    const QByteArray plain1 = QByteArray::fromHex(kPlain1);
    QString err;

    check(dmghg::wireMask(secret, kSlot) == QByteArray::fromHex(kWireMask),
          "wireMask(secret, slot) 与真实信封互证");
    check(dmghg::integrityTag(secret, env1.left(env1.size() - 32)) == QByteArray::fromHex(kTag1),
          "integrityTag 匹配真实信封尾部 32B");

    auto opened = dmghg::openEnvelope(secret, env1);
    check(opened && *opened == plain1, "openEnvelope 解出真实请求明文 (TEA/XOR/RC4)");
    if (!opened) std::printf("   env1 error: %s\n", err.toUtf8().constData());

    auto live = dmghg::openEnvelope(secret, QByteArray::fromHex(kLiveEnv), &err);
    check(live && live->startsWith('{') && live->contains("\"code\""),
          "openEnvelope 解出含 Blowfish+Base64 链的真实响应");
    if (!live) std::printf("   live error: %s\n", err.toUtf8().constData());

    const QByteArray probe = "{\"videoId\":\"336679146642399232\",\"pageIndex\":1}";
    const QByteArray sealed = dmghg::sealEnvelope(secret, probe, 1790018780);
    auto back = dmghg::openEnvelope(secret, sealed);
    check(!sealed.isEmpty() && back && *back == probe, "sealEnvelope -> openEnvelope 往返一致");
    check(sealed.left(1) == QByteArray(1, char(3)), "sealEnvelope 版本字节 03");

    const QByteArray txt("the quick brown fox jumps over 13 lazy dogs!!");
    check(dmghg::xorDecrypt(QByteArray(8, 'k'), dmghg::xorEncrypt(QByteArray(8, 'k'), txt)) == txt,
          "MagicXor 往返");
    check(dmghg::rc4(QByteArray(16, 'k'), dmghg::rc4(QByteArray(16, 'k'), txt)) == txt,
          "MagicRc4 自逆");
    check(dmghg::chacha(QByteArray(44, 'k'), dmghg::chacha(QByteArray(44, 'k'), txt)) == txt,
          "MagicChaCha20 自逆 (44B 密钥)");
    // 多块 ChaCha 才是真检查：块 0 在"feed-forward 用初始计数器"和"用递增至计数器"
    // 两种规则下逐字节相同，只有第 2 块起的第 48 字节才分道。参照值取自离线实现
    // magic_chain.chacha —— 那条实现用服务端 JSON 解析器当预言机定过规则，
    // 见 D:\project\ida\dm\scripts\chacha_rule_oracle.py。
    const QByteArray ccKey = QByteArray::fromHex(
        "030a11181f262d343b424950575e656c737a81888f969da4abb2b9c0c7ced5dce3eaf1f8ff060d141b222930");
    const QByteArray ccIn = QByteArray::fromHex(
        "0524436281a0bfdefd1c3b5a7998b7d6f51433527190afceed0c2b4a6988a7c6e504234261809fbeddfc1b3a597897b6"
        "d5f4133251708faecdec0b2a496887a6c5e4032241607f9ebddcfb1a39587796b5d4f31231506f8eadcceb0a29486786a5"
        "c4e30221405f7e9dbcdbfa1938577695b4d3f211304f6e8daccbea0928476685a4c3e201203f5e7d9cbbdaf91837567594"
        "b3d2f1102f4e6d8cabcae90827466584a3c2e1001f3e5d7c9bbad9f81736557493b2d1f00f2e4d6c8baac9e80726456483"
        "a2c1e0ff1e");
    const QByteArray ccWant = QByteArray::fromHex(
        "b3f478a8e73deb1152854a6cdd83f221bd5d13573e304e50a7cb07abc47fd25179a5850d8b0c31470b2838db418bc16715"
        "6fc044cd6e807a9754d582674f0abc368f4c1d22bcc8f240d3686b63d9af8ee348cb39103a3f4c0a1164b6972dc108b31eb"
        "d534e33c1410605b21e17511402505531e3e32152499c2b2752024821e7a0f9bb1563f5c482c40137b5d7093c06e5a45263"
        "4113fd63c1c155ab0f35d8e56e86c6c339e34378e2ab9057346e5470dd9dfae6bac3a93c68ac87b85c1aa613dc9cc706d8a4"
        "177c");
    check(dmghg::chacha(ccKey, ccIn) == ccWant,
          "MagicChaCha20 4 块 200B 与离线参照逐字节一致");
    check(dmghg::teaDecrypt(QByteArray(16, 'k'), dmghg::teaEncrypt(QByteArray(16, 'k'), txt)) == txt,
          "MagicTea 往返 (PKCS7 在核内)");
    check(dmghg::bfDecrypt(QByteArray(16, 'k'), dmghg::bfEncrypt(QByteArray(16, 'k'), txt)) == txt,
          "MagicBlowfish 往返 (替换 P/S 表)");
    bool b64ok = true;
    for (int n = 1; n <= 9; ++n) {
        const QByteArray s(n, 'A');
        if (dmghg::b64Decode(dmghg::b64Encode(s)) != s) b64ok = false;
    }
    check(b64ok && dmghg::b64Encode(QByteArray(3, 'A')).size() == 4,
          "MagicBase64 各长度往返 (无 = 填充)");

    const QByteArray jpeg = QByteArray::fromBase64(QByteArray(kBootJpeg));
    auto cfg = dmghg::decodeBootstrap(jpeg, QByteArray(kAppId), 690, &err);
    check(cfg && cfg->masterSecret == QString(kBootMasterSecret),
          "decodeBootstrap 解出真实 masterSecret");
    check(cfg && cfg->signSecret == QString(kBootSignSecret),
          "decodeBootstrap 解出真实 signSecret");
    if (!cfg) std::printf("   bootstrap error: %s\n", err.toUtf8().constData());

    check(dmghg::deriveSecret(QString(kBootMasterSecret), QString(kDeviceId)) == secret,
          "deriveSecret == 运行时真实 secret");
    check(dmghg::makeSignature(qint64(1790018753542),
                               QStringLiteral("9751fb918ff343bdb6ca2b4e50f109a6"),
                               QString(kAppId), QString(kBootSignSecret))
              == QStringLiteral("98b26bd878d58cd7908a81aa497dc9ce"),
          "makeSignature 复现 app 真实 X-Signature");

    std::printf("\n%s\n", g_fail ? "SOME CHECKS FAILED" : "all magic_sdk checks passed");
    return g_fail ? 1 : 0;
}
