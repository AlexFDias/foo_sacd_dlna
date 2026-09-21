#include <cctype>
#include <cstdio>
#include <string>
#include "soap_real.inc"
static int fails = 0, checks = 0;
#define EXPECT(header, body, want) do { ++checks; const std::string got = soapActionName(header, body); if (got != want) { ++fails; std::printf("FAIL: header=%s body=%.40s -> '%s' (wanted '%s')\n", std::string(header).c_str(), std::string(body).c_str(), got.c_str(), want); } } while (0)
int main() {
    const char* CD = "urn:schemas-upnp-org:service:ContentDirectory:1";
    const std::string env = "<?xml version=\"1.0\"?><s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\"><s:Body>";
    // header present (with and without quotes, CRLF left over from the header parser)
    EXPECT(std::string("\"") + CD + "#Browse\"", "", "Browse");
    EXPECT(std::string(CD) + "#Browse", "", "Browse");
    EXPECT(std::string("\"") + CD + "#GetSearchCapabilities\"\r\n", "", "GetSearchCapabilities");
    EXPECT(std::string("\"") + CD + "#GetSortCapabilities\"", "", "GetSortCapabilities");
    EXPECT(std::string("\"") + CD + "#GetSystemUpdateID\"", "", "GetSystemUpdateID");
    EXPECT(std::string("\"") + CD + "#Search\"", "", "Search");
    EXPECT("\"urn:schemas-upnp-org:service:ContentDirectory:1#Browse\" ", "", "Browse");
    // header wins over the body
    EXPECT(std::string("\"") + CD + "#Browse\"", env + "<u:GetSortCapabilities/>", "Browse");
    // no header: first element inside the body, any prefix
    EXPECT("", env + "<u:Browse xmlns:u=\"" + CD + "\"><ObjectID>0</ObjectID></u:Browse></s:Body></s:Envelope>", "Browse");
    EXPECT("", env + "<ns0:GetSortCapabilities xmlns:ns0=\"" + CD + "\"/></s:Body></s:Envelope>", "GetSortCapabilities");
    EXPECT("", env + "<GetSystemUpdateID xmlns=\"" + CD + "\"></GetSystemUpdateID>", "GetSystemUpdateID");
    EXPECT("", "<soap:Envelope xmlns:soap=\"x\"><soap:Header/><soap:Body><m:Browse xmlns:m=\"y\"/></soap:Body></soap:Envelope>", "Browse");
    EXPECT("", "<?xml version=\"1.0\"?>\n<s:Envelope\n xmlns:s=\"x\">\n<s:Body>\n<u:GetSearchCapabilities\n xmlns:u=\"y\"/>", "GetSearchCapabilities");
    // nothing usable
    EXPECT("", "", "");
    EXPECT("", "garbage", "");
    EXPECT("", "<?xml version=\"1.0\"?>", "");
    std::printf("%d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
