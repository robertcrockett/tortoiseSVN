/* ====================================================================
 *    Licensed to the Apache Software Foundation (ASF) under one
 *    or more contributor license agreements.  See the NOTICE file
 *    distributed with this work for additional information
 *    regarding copyright ownership.  The ASF licenses this file
 *    to you under the Apache License, Version 2.0 (the
 *    "License"); you may not use this file except in compliance
 *    with the License.  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *    Unless required by applicable law or agreed to in writing,
 *    software distributed under the License is distributed on an
 *    "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 *    KIND, either express or implied.  See the License for the
 *    specific language governing permissions and limitations
 *    under the License.
 * ====================================================================
 */

#include "serf.h"
#include "test_server.h"

#include "serf_private.h"

#include <openssl/bio.h>
#include <openssl/ssl.h>
#include <openssl/err.h>

#if defined(OPENSSL_VERSION_NUMBER) && OPENSSL_VERSION_NUMBER >= 0x10100000L
#define USE_OPENSSL_1_1_API
#endif

static int init_done = 0;

typedef struct ssl_context_t {
    int handshake_done;
    int hit_eof;

    SSL_CTX* ctx;
    SSL* ssl;
    BIO *bio;
    BIO_METHOD *biom;

} ssl_context_t;

static int err_file_print_cb(const char *str, size_t len, void *bp)
{
    return fwrite(str, 1, len, bp);
}

static int pem_passwd_cb(char *buf, int size, int rwflag, void *userdata)
{
    strncpy(buf, "serftest", size);
    buf[size - 1] = '\0';
    return strlen(buf);
}

static void bio_set_data(BIO *bio, void *data)
{
#ifdef USE_OPENSSL_1_1_API
    BIO_set_data(bio, data);
#else
    bio->ptr = data;
#endif
}

static void *bio_get_data(BIO *bio)
{
#ifdef USE_OPENSSL_1_1_API
    return BIO_get_data(bio);
#else
    return bio->ptr;
#endif
}

static int bio_apr_socket_create(BIO *bio)
{
#ifdef USE_OPENSSL_1_1_API
    BIO_set_shutdown(bio, 1);
    BIO_set_init(bio, 1);
    BIO_set_data(bio, NULL);
#else
    bio->shutdown = 1;
    bio->init = 1;
    bio->num = -1;
    bio->ptr = NULL;
#endif

    return 1;
}

static int bio_apr_socket_destroy(BIO *bio)
{
    /* Did we already free this? */
    if (bio == NULL) {
        return 0;
    }

    return 1;
}

static long bio_apr_socket_ctrl(BIO *bio, int cmd, long num, void *ptr)
{
    serv_ctx_t *serv_ctx = bio_get_data(bio);
    ssl_context_t *ssl_ctx = serv_ctx->ssl_ctx;

    switch (cmd) {
        case BIO_CTRL_FLUSH:
            /* At this point we can't force a flush. */
            return 1;
        case BIO_CTRL_PUSH:
        case BIO_CTRL_POP:
            return 0;
        case BIO_CTRL_EOF:
            return ssl_ctx->hit_eof;
        default:
            /* abort(); */
            return 0;
    }
}

/* Returns the amount read. */
static int bio_apr_socket_read(BIO *bio, char *in, int inlen)
{
    apr_size_t len = inlen;
    serv_ctx_t *serv_ctx = bio_get_data(bio);
    apr_status_t status;

    BIO_clear_retry_flags(bio);

    status = apr_socket_recv(serv_ctx->client_sock, in, &len);
    serv_ctx->bio_read_status = status;
    serf__log_skt(TEST_VERBOSE, __FILE__, serv_ctx->client_sock,
                  "Read %d bytes from socket with status %d.\n", len, status);

    if (APR_STATUS_IS_EOF(status)) {
        serv_ctx->hit_eof = 1;
    }

    if (status == APR_EAGAIN) {
        BIO_set_retry_read(bio);
        if (len == 0)
            return -1;
    }

    if (SERF_BUCKET_READ_ERROR(status))
        return -1;

    return len;
}

/* Returns the amount written. */
static int bio_apr_socket_write(BIO *bio, const char *in, int inlen)
{
    apr_size_t len = inlen;
    serv_ctx_t *serv_ctx = bio_get_data(bio);

    apr_status_t status = apr_socket_send(serv_ctx->client_sock, in, &len);

    serf__log_skt(TEST_VERBOSE, __FILE__, serv_ctx->client_sock,
                  "Wrote %d of %d bytes to socket with status %d.\n",
                  len, inlen, status);

    if (SERF_BUCKET_READ_ERROR(status))
        return -1;

    return len;
}


#ifndef USE_OPENSSL_1_1_API
static BIO_METHOD bio_apr_socket_method = {
    BIO_TYPE_SOCKET,
    "APR sockets",
    bio_apr_socket_write,
    bio_apr_socket_read,
    NULL,                        /* Is this called? */
    NULL,                        /* Is this called? */
    bio_apr_socket_ctrl,
    bio_apr_socket_create,
    bio_apr_socket_destroy,
#ifdef OPENSSL_VERSION_NUMBER
    NULL /* sslc does not have the callback_ctrl field */
#endif
};
#endif

static BIO_METHOD *bio_meth_apr_socket_new(void)
{
    BIO_METHOD *biom = NULL;

#ifdef USE_OPENSSL_1_1_API
    biom = BIO_meth_new(BIO_TYPE_SOCKET, "APR sockets");
    if (biom) {
        BIO_meth_set_write(biom, bio_apr_socket_write);
        BIO_meth_set_read(biom, bio_apr_socket_read);
        BIO_meth_set_ctrl(biom, bio_apr_socket_ctrl);
        BIO_meth_set_create(biom, bio_apr_socket_create);
        BIO_meth_set_destroy(biom, bio_apr_socket_destroy);
    }
#else
    biom = &bio_apr_socket_method;
#endif

    return biom;
}

static int validate_client_certificate(int preverify_ok, X509_STORE_CTX *ctx)
{
    serf__log(TEST_VERBOSE, __FILE__, "validate_client_certificate called, "
              "preverify code: %d.\n", preverify_ok);

    return preverify_ok;
}

static apr_status_t init_ssl(serv_ctx_t *serv_ctx)
{
    ssl_context_t *ssl_ctx = serv_ctx->ssl_ctx;

    ssl_ctx->ssl = SSL_new(ssl_ctx->ctx);
    SSL_set_cipher_list(ssl_ctx->ssl, "ALL");
    SSL_set_bio(ssl_ctx->ssl, ssl_ctx->bio, ssl_ctx->bio);

    return APR_SUCCESS;
}

static apr_status_t
init_ssl_context(serv_ctx_t *serv_ctx,
                 const char *keyfile,
                 const char **certfiles,
                 const char *client_cn)
{
    ssl_context_t *ssl_ctx = apr_pcalloc(serv_ctx->pool, sizeof(*ssl_ctx));
    serv_ctx->ssl_ctx = ssl_ctx;
    serv_ctx->client_cn = client_cn;
    serv_ctx->bio_read_status = APR_SUCCESS;

    /* Init OpenSSL globally */
    if (!init_done)
    {
#ifdef USE_OPENSSL_1_1_API
        OPENSSL_malloc_init();
#else
        CRYPTO_malloc_init();
#endif
        ERR_load_crypto_strings();
        SSL_load_error_strings();
        SSL_library_init();
        OpenSSL_add_all_algorithms();
        init_done = 1;
    }

    /* Init this connection */
    if (!ssl_ctx->ctx) {
        X509_STORE *store;
        const char *certfile;
        int i;
        int rv;

        ssl_ctx->ctx = SSL_CTX_new(SSLv23_server_method());
        SSL_CTX_set_default_passwd_cb(ssl_ctx->ctx, pem_passwd_cb);
        rv = SSL_CTX_use_PrivateKey_file(ssl_ctx->ctx, keyfile,
                                         SSL_FILETYPE_PEM);
        if (rv != 1) {
            fprintf(stderr, "Cannot load private key from file '%s'\n", keyfile);
            exit(1);
        }

        /* Set server certificate, add ca certificates if provided. */
        certfile = certfiles[0];
        rv = SSL_CTX_use_certificate_file(ssl_ctx->ctx, certfile, SSL_FILETYPE_PEM);
        if (rv != 1) {
            fprintf(stderr, "Cannot load certficate from file '%s'\n", certfile);
            exit(1);
        }

        i = 1;
        certfile = certfiles[i++];
        store = SSL_CTX_get_cert_store(ssl_ctx->ctx);

        while(certfile) {
            BIO *bio = BIO_new_file(certfile, "r");
            if (bio) {
                X509 *ssl_cert = PEM_read_bio_X509(bio, NULL, NULL, NULL);
                BIO_free(bio);

                SSL_CTX_add_extra_chain_cert(ssl_ctx->ctx, ssl_cert);

                X509_STORE_add_cert(store, ssl_cert);
            }
            certfile = certfiles[i++];
        }

        /* This makes the server send a client certificate request during
           handshake. The client certificate is optional (most tests don't
           send one) by default, but mandatory if client_cn was specified. */
        SSL_CTX_set_verify(ssl_ctx->ctx, SSL_VERIFY_PEER,
                           validate_client_certificate);

        SSL_CTX_set_mode(ssl_ctx->ctx, SSL_MODE_ENABLE_PARTIAL_WRITE);

        ssl_ctx->biom = bio_meth_apr_socket_new();
        ssl_ctx->bio = BIO_new(ssl_ctx->biom);
        bio_set_data(ssl_ctx->bio, serv_ctx);
        init_ssl(serv_ctx);
    }

    return APR_SUCCESS;
}

static apr_status_t ssl_reset(serv_ctx_t *serv_ctx)
{
    ssl_context_t *ssl_ctx = serv_ctx->ssl_ctx;

    serf__log(TEST_VERBOSE, __FILE__, "Reset ssl context.\n");

    ssl_ctx->handshake_done = 0;
    ssl_ctx->hit_eof = 0;
    if (ssl_ctx)
        SSL_clear(ssl_ctx->ssl);
    init_ssl(serv_ctx);

    return APR_SUCCESS;
}

static apr_status_t ssl_handshake(serv_ctx_t *serv_ctx)
{
    ssl_context_t *ssl_ctx = serv_ctx->ssl_ctx;
    int result;

    if (ssl_ctx->handshake_done)
        return APR_SUCCESS;

    /* SSL handshake */
    result = SSL_accept(ssl_ctx->ssl);
    if (result == 1) {
        X509 *peer;

        serf__log(TEST_VERBOSE, __FILE__, "Handshake successful.\n");

        /* Check client certificate */
        peer = SSL_get_peer_certificate(ssl_ctx->ssl);
        if (peer)
        {
            serf__log(TEST_VERBOSE, __FILE__, "Peer cert received.\n");
            if (SSL_get_verify_result(ssl_ctx->ssl) == X509_V_OK)
            {
                /* The client sent a certificate which verified OK */
                char buf[1024];
                int ret;
                X509_NAME *subject = X509_get_subject_name(peer);

                ret = X509_NAME_get_text_by_NID(subject,
                                                NID_commonName,
                                                buf, 1024);
                if (ret != -1 && strcmp(serv_ctx->client_cn, buf) != 0) {
                    serf__log(TEST_VERBOSE, __FILE__, "Client cert common name "
                              "\"%s\" doesn't match expected \"%s\".\n", buf,
                              serv_ctx->client_cn);
                    return SERF_ERROR_ISSUE_IN_TESTSUITE;

                }
            }
        } else {
            if (serv_ctx->client_cn) {
                serf__log(TEST_VERBOSE, __FILE__, "Client cert expected but not"
                          " received.\n");
                return SERF_ERROR_ISSUE_IN_TESTSUITE;
            }
        }

        ssl_ctx->handshake_done = 1;
    }
    else {
        int ssl_err;

        ssl_err = SSL_get_error(ssl_ctx->ssl, result);
        switch (ssl_err) {
            case SSL_ERROR_WANT_READ:
            case SSL_ERROR_WANT_WRITE:
                return APR_EAGAIN;
            case SSL_ERROR_SYSCALL:
                return serv_ctx->bio_read_status; /* Usually APR_EAGAIN */
            default:
                serf__log(TEST_VERBOSE, __FILE__, "SSL Error %d: ", ssl_err);
                ERR_print_errors_cb(err_file_print_cb, stderr);
                serf__log_nopref(TEST_VERBOSE, "\n");
                return SERF_ERROR_ISSUE_IN_TESTSUITE;
        }
    }

    return APR_EAGAIN;
}

static apr_status_t
ssl_socket_write(serv_ctx_t *serv_ctx, const char *data,
                 apr_size_t *len)
{
    ssl_context_t *ssl_ctx = serv_ctx->ssl_ctx;

    int result = SSL_write(ssl_ctx->ssl, data, *len);
    if (result > 0) {
        *len = result;
        return APR_SUCCESS;
    }

    if (result == 0)
        return APR_EAGAIN;

    serf__log(TEST_VERBOSE, __FILE__, "ssl_socket_write: ssl error?\n");

    return SERF_ERROR_ISSUE_IN_TESTSUITE;
}

static apr_status_t
ssl_socket_read(serv_ctx_t *serv_ctx, char *data,
                apr_size_t *len)
{
    ssl_context_t *ssl_ctx = serv_ctx->ssl_ctx;

    int result = SSL_read(ssl_ctx->ssl, data, *len);
    if (result > 0) {
        *len = result;
        return APR_SUCCESS;
    } else {
        int ssl_err;

        ssl_err = SSL_get_error(ssl_ctx->ssl, result);
        switch (ssl_err) {
            case SSL_ERROR_SYSCALL:
                /* error in bio_bucket_read, probably APR_EAGAIN or APR_EOF */
                *len = 0;
                return serv_ctx->bio_read_status;
            case SSL_ERROR_WANT_READ:
                *len = 0;
                return APR_EAGAIN;
            case SSL_ERROR_SSL:
            default:
                *len = 0;
                serf__log(TEST_VERBOSE, __FILE__,
                          "ssl_socket_read SSL Error %d: ", ssl_err);
                ERR_print_errors_cb(err_file_print_cb, stderr);
                serf__log_nopref(TEST_VERBOSE, "\n");
                return SERF_ERROR_ISSUE_IN_TESTSUITE;
        }
    }

    /* not reachable */
    return SERF_ERROR_ISSUE_IN_TESTSUITE;
}

static apr_status_t cleanup_https_server(void *baton)
{
    serv_ctx_t *servctx = baton;
    ssl_context_t *ssl_ctx = servctx->ssl_ctx;

    if (ssl_ctx) {
        if (ssl_ctx->ssl) {
          SSL_clear(ssl_ctx->ssl);
#ifdef USE_OPENSSL_1_1_API
          BIO_meth_free(ssl_ctx->biom);
#endif
        }
        SSL_CTX_free(ssl_ctx->ctx);
    }

    return APR_SUCCESS;
}

void setup_https_test_server(serv_ctx_t **servctx_p,
                             apr_sockaddr_t *address,
                             test_server_message_t *message_list,
                             apr_size_t message_count,
                             test_server_action_t *action_list,
                             apr_size_t action_count,
                             apr_int32_t options,
                             const char *keyfile,
                             const char **certfiles,
                             const char *client_cn,
                             apr_pool_t *pool)
{
    serv_ctx_t *servctx;

    setup_test_server(servctx_p, address, message_list,
                      message_count, action_list, action_count,
                      options, pool);
    servctx = *servctx_p;
    apr_pool_cleanup_register(pool, servctx,
                              cleanup_https_server,
                              apr_pool_cleanup_null);

    servctx->handshake = ssl_handshake;
    servctx->reset = ssl_reset;

    /* Override with SSL encrypt/decrypt functions */
    servctx->read = ssl_socket_read;
    servctx->send = ssl_socket_write;

    init_ssl_context(servctx, keyfile, certfiles, client_cn);
}
