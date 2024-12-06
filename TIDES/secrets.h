#define SECRET
#define THINGNAME "TideThing"

const char WIFI_SSID[] = "Enter WiFi SSID Here";
const char WIFI_PASSWORD[] = "Enter WiFi password here";  //TideSim2


const char EAP_IDENTITY[] = "";
const char EAP_PASSWORD[] = "";
const char AWS_IOT_ENDPOINT[] = "";


// Amazon Root CA 1 (this does not change)
static const char AWS_CERT_CA[] = R"EOF(
-----BEGIN CERTIFICATE-----

-----END CERTIFICATE-----
)EOF";

// Device Certificate 
static const char AWS_CERT_CRT[] = R"KEY(
-----BEGIN CERTIFICATE-----

-----END CERTIFICATE-----
)KEY";

// Device Private Key for TideSim2
static const char AWS_CERT_PRIVATE[] = R"KEY(
-----BEGIN RSA PRIVATE KEY-----

-----END RSA PRIVATE KEY-----
)KEY";
