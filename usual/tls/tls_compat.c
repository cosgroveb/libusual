/*
 * LibreSSL compat for other OpenSSL's.
 */

#include "tls_compat.h"

#ifdef USUAL_LIBSSL_FOR_TLS

#include <openssl/ec.h>


#ifndef SSL_CTX_set_dh_auto

/*
 * SKIP primes, used by OpenSSL and PostgreSQL.
 *
 * https://tools.ietf.org/html/draft-ietf-ipsec-skip-06
 */

static const char file_dh1024[] =
"-----BEGIN DH PARAMETERS-----\n"
"MIGHAoGBAPSI/VhOSdvNILSd5JEHNmszbDgNRR0PfIizHHxbLY7288kjwEPwpVsY\n"
"jY67VYy4XTjTNP18F1dDox0YbN4zISy1Kv884bEpQBgRjXyEpwpy1obEAxnIByl6\n"
"ypUM2Zafq9AKUJsCRtMIPWakXUGfnHy9iUsiGSa6q6Jew1XpL3jHAgEC\n"
"-----END DH PARAMETERS-----\n";

static const char file_dh2048[] =
"-----BEGIN DH PARAMETERS-----\n"
"MIIBCAKCAQEA9kJXtwh/CBdyorrWqULzBej5UxE5T7bxbrlLOCDaAadWoxTpj0BV\n"
"89AHxstDqZSt90xkhkn4DIO9ZekX1KHTUPj1WV/cdlJPPT2N286Z4VeSWc39uK50\n"
"T8X8dryDxUcwYc58yWb/Ffm7/ZFexwGq01uejaClcjrUGvC/RgBYK+X0iP1YTknb\n"
"zSC0neSRBzZrM2w4DUUdD3yIsxx8Wy2O9vPJI8BD8KVbGI2Ou1WMuF040zT9fBdX\n"
"Q6MdGGzeMyEstSr/POGxKUAYEY18hKcKctaGxAMZyAcpesqVDNmWn6vQClCbAkbT\n"
"CD1mpF1Bn5x8vYlLIhkmuquiXsNV6TILOwIBAg==\n"
"-----END DH PARAMETERS-----\n";

static const char file_dh4096[] =
"-----BEGIN DH PARAMETERS-----\n"
"MIICCAKCAgEA+hRyUsFN4VpJ1O8JLcCo/VWr19k3BCgJ4uk+d+KhehjdRqNDNyOQ\n"
"l/MOyQNQfWXPeGKmOmIig6Ev/nm6Nf9Z2B1h3R4hExf+zTiHnvVPeRBhjdQi81rt\n"
"Xeoh6TNrSBIKIHfUJWBh3va0TxxjQIs6IZOLeVNRLMqzeylWqMf49HsIXqbcokUS\n"
"Vt1BkvLdW48j8PPv5DsKRN3tloTxqDJGo9tKvj1Fuk74A+Xda1kNhB7KFlqMyN98\n"
"VETEJ6c7KpfOo30mnK30wqw3S8OtaIR/maYX72tGOno2ehFDkq3pnPtEbD2CScxc\n"
"alJC+EL7RPk5c/tgeTvCngvc1KZn92Y//EI7G9tPZtylj2b56sHtMftIoYJ9+ODM\n"
"sccD5Piz/rejE3Ome8EOOceUSCYAhXn8b3qvxVI1ddd1pED6FHRhFvLrZxFvBEM9\n"
"ERRMp5QqOaHJkM+Dxv8Cj6MqrCbfC4u+ZErxodzuusgDgvZiLF22uxMZbobFWyte\n"
"OvOzKGtwcTqO/1wV5gKkzu1ZVswVUQd5Gg8lJicwqRWyyNRczDDoG9jVDxmogKTH\n"
"AaqLulO7R8Ifa1SwF2DteSGVtgWEN8gDpN3RBmmPTDngyF2DHb5qmpnznwtFKdTL\n"
"KWbuHn491xNO25CQWMtem80uKw+pTnisBRF/454n1Jnhub144YRBoN8CAQI=\n"
"-----END DH PARAMETERS-----\n";


static DH *dh1024, *dh2048, *dh4096;

static DH *load_dh_buffer(DH **dhp, const char *buf)
{
	BIO *bio;
	DH *dh = *dhp;
	if (dh == NULL) {
		bio = BIO_new_mem_buf((char *)buf, strlen(buf));
		if (bio) {
			dh = PEM_read_bio_DHparams(bio, NULL, NULL, NULL);
			BIO_free(bio);
		}
		*dhp = dh;
	}
	return dh;
}

static DH *dh_auto_cb(SSL *s, int is_export, int keylength)
{
	EVP_PKEY *pk;
	int bits;

	pk = SSL_get_privatekey(s);
	if (!pk)
		return load_dh_buffer(&dh2048, file_dh2048);

	bits = EVP_PKEY_bits(pk);
	if (bits >= 3072)
		return load_dh_buffer(&dh4096, file_dh4096);
	if (bits >= 1536)
		return load_dh_buffer(&dh2048, file_dh2048);
	return load_dh_buffer(&dh1024, file_dh1024);
}

static DH *dh_legacy_cb(SSL *s, int is_export, int keylength)
{
	return load_dh_buffer(&dh1024, file_dh1024);
}

long SSL_CTX_set_dh_auto(SSL_CTX *ctx, int onoff)
{
	if (onoff == 0)
		return 1;
	if (onoff == 2) {
		SSL_CTX_set_tmp_dh_callback(ctx, dh_legacy_cb);
	} else {
		SSL_CTX_set_tmp_dh_callback(ctx, dh_auto_cb);
	}
	SSL_CTX_set_options(ctx, SSL_OP_SINGLE_DH_USE);
	return 1;
}

#endif

#ifndef SSL_CTX_set_ecdh_auto

/*
 * Use same curve as EC key, fallback to NIST P-256.
 */

static EC_KEY *ecdh_auto_cb(SSL *ssl, int is_export, int keylength)
{
	static EC_KEY *ecdh;
	int last_nid;
	int nid = 0;
	EVP_PKEY *pk;
	EC_KEY *ec;

	pk = SSL_get_privatekey(ssl);
	if (pk && pk->type == EVP_PKEY_EC) {
		ec = EVP_PKEY_get1_EC_KEY(pk);
		if (ec) {
			nid = EC_GROUP_get_curve_name(EC_KEY_get0_group(ec));
			EC_KEY_free(ec);
		}
	}
	if (nid == 0)
		nid = NID_X9_62_prime256v1;

	if (ecdh) {
		last_nid = EC_GROUP_get_curve_name(EC_KEY_get0_group(ecdh));
		if (last_nid == nid)
			return ecdh;
		EC_KEY_free(ecdh);
		ecdh = NULL;
	}

	ecdh = EC_KEY_new_by_curve_name(nid);
	return ecdh;
}

long SSL_CTX_set_ecdh_auto(SSL_CTX *ctx, int onoff)
{
	if (onoff) {
		SSL_CTX_set_options(ctx, SSL_OP_SINGLE_ECDH_USE);
		SSL_CTX_set_tmp_ecdh_callback(ctx, ecdh_auto_cb);
	}
	return 1;
}

#endif

#else /* !USUAL_LIBSSL_FOR_TLS */

/*
 * Install empty functions when openssl is not available.
 */

int tls_init(void) { return -1; }

const char *tls_error(struct tls *_ctx) { return "No TLS support"; }

struct tls_config *tls_config_new(void) { return NULL; }
void tls_config_free(struct tls_config *_config) {}

int tls_config_set_ca_file(struct tls_config *_config, const char *_ca_file) { return -1; }
int tls_config_set_ca_path(struct tls_config *_config, const char *_ca_path) { return -1; }
int tls_config_set_ca_mem(struct tls_config *_config, const uint8_t *_ca, size_t _len) { return -1; }
int tls_config_set_cert_file(struct tls_config *_config, const char *_cert_file) { return -1; }
int tls_config_set_cert_mem(struct tls_config *_config, const uint8_t *_cert, size_t _len) { return -1; }
int tls_config_set_ciphers(struct tls_config *_config, const char *_ciphers) { return -1; }
int tls_config_set_dheparams(struct tls_config *_config, const char *_params) { return -1; }
int tls_config_set_ecdhecurve(struct tls_config *_config, const char *_name) { return -1; }
int tls_config_set_key_file(struct tls_config *_config, const char *_key_file) { return -1; }
int tls_config_set_key_mem(struct tls_config *_config, const uint8_t *_key, size_t _len) { return -1; }
void tls_config_set_protocols(struct tls_config *_config, uint32_t _protocols) {}
void tls_config_set_verify_depth(struct tls_config *_config, int _verify_depth) {}

void tls_config_clear_keys(struct tls_config *_config) {}
int tls_config_parse_protocols(uint32_t *_protocols, const char *_protostr) { return -1; }

void tls_config_insecure_noverifycert(struct tls_config *_config) {}
void tls_config_insecure_noverifyname(struct tls_config *_config) {}
void tls_config_verify(struct tls_config *_config) {}

struct tls *tls_client(void) { return NULL; }
struct tls *tls_server(void) { return NULL; }
int tls_configure(struct tls *_ctx, struct tls_config *_config) { return -1; }
void tls_reset(struct tls *_ctx) {}
void tls_free(struct tls *_ctx) {}

int tls_accept_fds(struct tls *_ctx, struct tls **_cctx, int _fd_read, int _fd_write) { return -1; }
int tls_accept_socket(struct tls *_ctx, struct tls **_cctx, int _socket) { return -1; }
int tls_connect(struct tls *_ctx, const char *_host, const char *_port) { return -1; }
int tls_connect_fds(struct tls *_ctx, int _fd_read, int _fd_write, const char *_servername) { return -1; }
int tls_connect_servername(struct tls *_ctx, const char *_host, const char *_port, const char *_servername) { return -1; }
int tls_connect_socket(struct tls *_ctx, int _s, const char *_servername) { return -1; }
int tls_read(struct tls *_ctx, void *_buf, size_t _buflen, size_t *_outlen) { return -1; }
int tls_write(struct tls *_ctx, const void *_buf, size_t _buflen, size_t *_outlen) { return -1; }
int tls_close(struct tls *_ctx) { return -1; }

int tls_get_connection_info(struct tls *ctx, char *buf, size_t buflen) { return -1; }

uint8_t *tls_load_file(const char *_file, size_t *_len, char *_password) { return NULL; }

int tls_get_peer_cert(struct tls *ctx, struct tls_cert_info **cert_p) { *cert_p = NULL; return -1; }
void tls_cert_free(struct tls_cert_info *cert) {}

#endif /* !USUAL_LIBSSL_FOR_TLS */
