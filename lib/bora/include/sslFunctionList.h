/*********************************************************
 * Copyright (C) 1998 VMware, Inc. All rights reserved.
 *
 * This file is part of VMware View Open Client.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published
 * by the Free Software Foundation version 2.1 and no later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE.  See the Lesser GNU General Public
 * License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin St, Fifth Floor, Boston, MA  02110-1301 USA.
 *
 *********************************************************/

/*
 * sslFunctionList.h --
 *
 *	List of all the SSL functions we use in convenient form for macro
 *	manipulation.
 */

#ifndef _SSL_FUNCTION_LIST_H_
#define _SSL_FUNCTION_LIST_H_

#if defined(VMCRYPTO_FIPS_COMPLIANT) || defined(OPENSSL_098)
#define SHA1_T1 size_t
#else
#define SHA1_T1 unsigned long
#endif

#ifdef OPENSSL_098
#define SSL_U_C_A_T1 const unsigned char *
#define SSL_C_U_C_A_T1 const unsigned char *
#define EVP_VF_T1 const unsigned char *
#define EVP_DU_T1 size_t
#define PEM_W_B_RPK_T1 const RSA *
#define D2I_PK_T1 const unsigned char **
#define D2I_X509_T1 const unsigned char **
#define HMAC_T1 size_t
#else
#define SSL_U_C_A_T1 unsigned char *
#define SSL_C_U_C_A_T1 unsigned char *
#define EVP_VF_T1 unsigned char *
#define EVP_DU_T1 unsigned int
#define PEM_W_B_RPK_T1 RSA *
#define D2I_PK_T1 unsigned char **
#define D2I_X509_T1 unsigned char **
#define HMAC_T1 int
#endif

#define VMW_SSL_VOID_FUNCTIONS_COMMON \
   VMW_SSL_FUNC(crypto, void, ERR_error_string_n, (unsigned long e, char *buf, size_t len), \
                (e, buf, len)) \
   VMW_SSL_FUNC(crypto, void, ERR_remove_state, (unsigned long pid), (pid)) \
   VMW_SSL_FUNC(crypto, void, RAND_seed, (const void *buf, int num), (buf, num)) \
   VMW_SSL_FUNC(ssl, void, SSL_load_error_strings, (void), ()) \
   VMW_SSL_FUNC(ssl, void, SSL_CTX_set_quiet_shutdown, (SSL_CTX *ctx, int mode), (ctx, mode)) \
   VMW_SSL_FUNC(ssl, void, SSL_CTX_set_tmp_dh_callback, \
                (SSL_CTX *ctx, DH *(*tmp_dh_callback)(SSL *, int, int)), \
                (ctx, tmp_dh_callback)) \
   VMW_SSL_FUNC(ssl, void, SSL_CTX_free, (SSL_CTX *ctx), (ctx)) \
   VMW_SSL_FUNC(ssl, void, SSL_set_connect_state, (SSL *s), (s)) \
   VMW_SSL_FUNC(ssl, void, SSL_set_accept_state, (SSL *s), (s)) \
   VMW_SSL_FUNC(ssl, void, SSL_free, (SSL *s), (s)) \
   VMW_SSL_FUNC(ssl, void, SSL_SESSION_free, (SSL_SESSION *ssl_session), (ssl_session)) \
   VMW_SSL_FUNC(ssl, void, SSL_CTX_set_default_passwd_cb, (SSL_CTX *ctx, pem_password_cb *cb), \
                (ctx, cb)) \
   VMW_SSL_FUNC(ssl, void, SSL_CTX_set_default_passwd_cb_userdata, (SSL_CTX *ctx, void *u), \
                (ctx, u)) \
   VMW_SSL_FUNC(ssl, void, SSL_CTX_set_verify, \
                 (SSL_CTX *ctx, int mode, int (*verify_callback)(int, X509_STORE_CTX *)), \
                 (ctx, mode, verify_callback)) \
   VMW_SSL_FUNC(crypto, void, DH_free, (DH *dh), (dh)) \
   VMW_SSL_FUNC(ssl, void, SSL_CTX_set_verify_depth, (SSL_CTX *ctx, int depth), (ctx, depth)) \
   VMW_SSL_FUNC(ssl, void, SSL_set_bio, (SSL *s, BIO *rbio, BIO *wbio), (s, rbio, wbio)) \
   VMW_SSL_FUNC(crypto, void, CRYPTO_set_locking_callback, \
                (void (*locking_callback)(int mode, int n, const char *file, int line)), \
                (locking_callback)) \
   VMW_SSL_FUNC(crypto, void, CRYPTO_set_id_callback, (unsigned long (*id_callback)(void)), \
                (id_callback)) \
   VMW_SSL_FUNC(crypto, void, CRYPTO_set_add_lock_callback, \
               (int (*fc)(int *num,int mount,int type, const char *file, int line)), (fc)) \
   VMW_SSL_FUNC(crypto, void, X509_free, (X509 *x), (x)) \
   VMW_SSL_FUNC(crypto, void, RSA_free, (RSA *r), (r)) \
   VMW_SSL_FUNC(crypto, void, EVP_PKEY_free, (EVP_PKEY *pkey), (pkey)) \
   VMW_SSL_FUNC(crypto, void, sk_pop_free, (STACK *st, void (*stfunc)(void *)), (st, stfunc)) \
   VMW_SSL_FUNC(crypto, void, OpenSSL_add_all_ciphers, (void), ()) \
   VMW_SSL_FUNC(ssl, void, SSL_set_verify, \
                 (SSL *s, int mode, int (*verify_callback)(int, X509_STORE_CTX *)), \
                 (s, mode, verify_callback)) \
   VMW_SSL_FUNC(crypto, void, DSA_free, \
                (DSA *dsa),  \
                (dsa)) \
   VMW_SSL_FUNC(crypto, void, ERR_clear_error, (void), ()) \
   VMW_SSL_FUNC(crypto, void, AES_encrypt, \
                (const unsigned char *in, unsigned char *out, \
                 const AES_KEY *key), \
                (in, out, key)) \
   VMW_SSL_FUNC(crypto, void, AES_decrypt, \
                (const unsigned char *in, unsigned char *out, \
                 const AES_KEY *key), \
                (in, out, key)) \
   VMW_SSL_FUNC(crypto, void, DES_ecb_encrypt, \
                (const_DES_cblock *input, DES_cblock *output, \
                 DES_key_schedule *ks, int enc), \
                (input, output, ks, enc)) \
   VMW_SSL_FUNC(crypto, void, OPENSSL_add_all_algorithms_noconf, (void), ()) \
   VMW_SSL_FUNC(crypto, void, ERR_load_crypto_strings, (void), ()) \
   VMW_SSL_FUNC(crypto, void, EVP_CIPHER_CTX_init, (EVP_CIPHER_CTX *a), (a)) \
   VMW_SSL_FUNC(crypto, void, EVP_MD_CTX_init, (EVP_MD_CTX *ctx), (ctx))

#define VMW_SSL_RET_FUNCTIONS_COMMON \
   VMW_SSL_FUNC(crypto, int, CRYPTO_num_locks, (void), ()) \
   VMW_SSL_FUNC(crypto, unsigned long, ERR_peek_error, (void), ()) \
   VMW_SSL_FUNC(crypto, char *, ERR_error_string, (unsigned long e, char *buf), (e, buf)) \
   VMW_SSL_FUNC(crypto, unsigned long, ERR_get_error, (void), ()) \
   VMW_SSL_FUNC(ssl, SSL_CIPHER *, SSL_get_current_cipher, (const SSL *s), (s)) \
   VMW_SSL_FUNC(ssl, int, SSL_CIPHER_get_bits, (const SSL_CIPHER *c, int *bits), (c, bits)) \
   VMW_SSL_FUNC(ssl, int, SSL_get_error, (const SSL *s, int ret_code), (s, ret_code)) \
   VMW_SSL_FUNC(ssl, long, SSL_get_verify_result, (const SSL *s), (s)) \
   VMW_SSL_FUNC(ssl, X509 *, SSL_get_peer_certificate, (const SSL *s), (s)) \
   VMW_SSL_FUNC(ssl, int, SSL_library_init, (void), ()) \
   VMW_SSL_FUNC(ssl, int, SSL_peek, (SSL *ssl, void *buf, int num), (ssl, buf, num)) \
   VMW_SSL_FUNC(ssl, SSL_SESSION *, SSL_get1_session, (SSL *ssl), (ssl)) \
   VMW_SSL_FUNC(ssl, int, SSL_set_session, (SSL *ssl, SSL_SESSION *session), (ssl, session)) \
   VMW_SSL_FUNC(ssl, SSL_METHOD *, SSLv2_method, (void), ()) \
   VMW_SSL_FUNC(ssl, SSL_METHOD *, SSLv3_method, (void), ()) \
   VMW_SSL_FUNC(ssl, SSL_METHOD *, SSLv23_method, (void), ()) \
   VMW_SSL_FUNC(ssl, SSL_CTX *, SSL_CTX_new, (SSL_METHOD *m), (m)) \
   VMW_SSL_FUNC(ssl, long, SSL_CTX_ctrl, (SSL_CTX *ctx, int cmd, long larg, void *parg), \
                (ctx, cmd, larg, parg)) \
   VMW_SSL_FUNC(ssl, int, SSL_CTX_use_certificate_chain_file, \
                (SSL_CTX *ctx, const char *file), (ctx, file)) \
   VMW_SSL_FUNC(ssl, int, SSL_CTX_use_certificate_file, \
                (SSL_CTX *ctx, const char *file, int type), \
                (ctx, file, type)) \
   VMW_SSL_FUNC(ssl, int, SSL_CTX_use_PrivateKey_file, \
                (SSL_CTX *ctx, const char *file, int type), \
                (ctx, file, type)) \
   VMW_SSL_FUNC(ssl, int, SSL_CTX_load_verify_locations, \
                (SSL_CTX *ctx, const char *CAfile, const char *CApath), \
                (ctx, CAfile, CApath)) \
   VMW_SSL_FUNC(ssl, int, SSL_CTX_set_default_verify_paths, (SSL_CTX *ctx), (ctx)) \
   VMW_SSL_FUNC(ssl, int, SSL_CTX_use_PrivateKey, (SSL_CTX *ctx, EVP_PKEY *pkey), (ctx, pkey)) \
   VMW_SSL_FUNC(ssl, int, SSL_CTX_use_certificate, (SSL_CTX *ctx, X509 *x), (ctx, x)) \
   VMW_SSL_FUNC(ssl, int, SSL_CTX_check_private_key, (const SSL_CTX *ctx), (ctx)) \
   VMW_SSL_FUNC(ssl, int, SSL_CTX_set_session_id_context, \
                (SSL_CTX *ctx, const unsigned char *sid_ctx, unsigned int sid_ctx_len), \
                (ctx, sid_ctx, sid_ctx_len)) \
   VMW_SSL_FUNC(ssl, SSL *, SSL_new, (SSL_CTX *ctx), (ctx)) \
   VMW_SSL_FUNC(ssl, long, SSL_ctrl, (SSL *s, int cmd, long larg, void *parg), (s, cmd, larg, parg)) \
   VMW_SSL_FUNC(ssl, int, SSL_set_fd, (SSL *s, int fd), (s, fd)) \
   VMW_SSL_FUNC(ssl, int, SSL_connect, (SSL *s), (s)) \
   VMW_SSL_FUNC(ssl, int, SSL_accept, (SSL *s), (s)) \
   VMW_SSL_FUNC(ssl, int, SSL_read, (SSL *s, void *buf, int size), (s, buf, size)) \
   VMW_SSL_FUNC(ssl, int, SSL_write, (SSL *s, const void *buf, int size), (s, buf, size)) \
   VMW_SSL_FUNC(ssl, int, SSL_shutdown, (SSL *s), (s)) \
   VMW_SSL_FUNC(ssl, int, SSL_pending, (const SSL *s), (s)) \
   VMW_SSL_FUNC(ssl, int, SSL_want, (const SSL *s), (s)) \
   VMW_SSL_FUNC(ssl, int, SSL_clear, (SSL *s), (s)) \
   VMW_SSL_FUNC(ssl, int, SSL_use_certificate_ASN1, (SSL *ssl, SSL_U_C_A_T1 d, int len), \
                (ssl, d, len)) \
   VMW_SSL_FUNC(ssl, int, SSL_CTX_use_certificate_ASN1, \
                (SSL_CTX *ctx, int len, SSL_C_U_C_A_T1 d), \
                (ctx, len, d)) \
   VMW_SSL_FUNC(ssl, int, SSL_CTX_set_cipher_list, (SSL_CTX *ctx, const char *str), \
                (ctx, str)) \
   VMW_SSL_FUNC(ssl, const char *, SSL_CIPHER_get_name, (const SSL_CIPHER *ciph), (ciph)) \
   VMW_SSL_FUNC(crypto, int, RAND_status, (void), ()) \
   VMW_SSL_FUNC(crypto, int, RAND_load_file, (const char *filename, long maxbytes), \
                (filename, maxbytes)) \
   VMW_SSL_FUNC(crypto, int, RAND_set_rand_method, (const RAND_METHOD *meth), (meth)) \
   VMW_SSL_FUNC(crypto, BIO *, BIO_new, (BIO_METHOD *type), (type)) \
   VMW_SSL_FUNC(crypto, BIO_METHOD *, BIO_s_mem, (void), ()) \
   VMW_SSL_FUNC(crypto, int, BIO_write, (BIO *b, const void *buf, int len), (b, buf, len)) \
   VMW_SSL_FUNC(crypto, int, BIO_free, (BIO *a), (a)) \
   VMW_SSL_FUNC(crypto, BIO_METHOD *, BIO_s_file, (void), ()) \
   VMW_SSL_FUNC(crypto, DH *, PEM_read_bio_DHparams, \
                (BIO *bp, DH **x, pem_password_cb *cb, void *u), \
                (bp, x, cb, u)) \
   VMW_SSL_FUNC(crypto, int, X509_STORE_CTX_get_error_depth, (X509_STORE_CTX *ctx), (ctx)) \
   VMW_SSL_FUNC(crypto, int, X509_STORE_CTX_get_error, (X509_STORE_CTX *ctx), (ctx)) \
   VMW_SSL_FUNC(crypto, X509 *, X509_STORE_CTX_get_current_cert, (X509_STORE_CTX *ctx), (ctx)) \
   VMW_SSL_FUNC(crypto, X509 *, PEM_read_bio_X509, \
                (BIO *bp, X509 **x, pem_password_cb *cb, void *u), \
                (bp, x, cb, u)) \
   VMW_SSL_FUNC(crypto, X509_NAME *, X509_get_issuer_name, (X509 *a), (a)) \
   VMW_SSL_FUNC(crypto, int, X509_NAME_get_text_by_NID, \
                (X509_NAME *name, int nid, char *buf, int len), \
                (name, nid, buf, len)) \
   VMW_SSL_FUNC(crypto, X509_NAME *, X509_get_subject_name, (X509 *x), (x)) \
   VMW_SSL_FUNC(crypto, int, X509_STORE_add_cert, (X509_STORE *ctx, X509 *x), (ctx, x)) \
   VMW_SSL_FUNC(crypto, X509_LOOKUP *, X509_STORE_add_lookup, \
                (X509_STORE *v, X509_LOOKUP_METHOD *m), (v, m)) \
   VMW_SSL_FUNC(crypto, const char *, X509_verify_cert_error_string, (long n), (n)) \
   VMW_SSL_FUNC(crypto, int, X509_LOOKUP_ctrl, \
                (X509_LOOKUP *ctx, int cmd, const char *argc, long argl, char **ret), \
                (ctx, cmd, argc, argl, ret)) \
   VMW_SSL_FUNC(crypto, char *, X509_NAME_oneline, (X509_NAME *a, char *buf, int size), \
                (a, buf, size)) \
   VMW_SSL_FUNC(crypto, int, i2d_X509_AUX, (X509 *a,unsigned char **pp), (a, pp)) \
   VMW_SSL_FUNC(crypto, BIO *, BIO_new_mem_buf, (void *buf, int len), (buf, len)) \
   VMW_SSL_FUNC(crypto, BIO_METHOD *, BIO_f_md, (void), ()) \
   VMW_SSL_FUNC(crypto, BIO *, BIO_new_file, (const char *filename, const char *mode), \
                (filename, mode)) \
   VMW_SSL_FUNC(crypto, BIO *, BIO_new_socket, (int sock, int close_flag), (sock, close_flag)) \
   VMW_SSL_FUNC(crypto, BIO *, BIO_push, (BIO *b, BIO *append), (b, append)) \
   VMW_SSL_FUNC(crypto, int, BIO_read, (BIO *b, void *data, int len), (b, data, len)) \
   VMW_SSL_FUNC(crypto, long, BIO_ctrl, (BIO *bp,int cmd,long larg,void *parg), \
                (bp, cmd, larg, parg)) \
   VMW_SSL_FUNC(crypto, int, RSA_padding_add_PKCS1_OAEP, \
                (unsigned char *to,int tlen, const unsigned char *f,int fl,\
                 const unsigned char *p,int pl), (to, tlen, f, fl, p, pl)) \
   VMW_SSL_FUNC(crypto, int, RSA_size, (const RSA *r), (r)) \
   VMW_SSL_FUNC(crypto, int, RSA_public_encrypt, \
                (int flen, const unsigned char *from, \
                 unsigned char *to, RSA *rsa, int padding), \
                (flen, from, to, rsa, padding)) \
   VMW_SSL_FUNC(crypto, int, RSA_private_encrypt, \
                (int flen, const unsigned char *from, \
                 unsigned char *to, RSA *rsa, int padding), \
                (flen, from, to, rsa, padding)) \
   VMW_SSL_FUNC(crypto, int, RSA_public_decrypt, \
                (int flen, const unsigned char *from, \
                 unsigned char *to, RSA *rsa, int padding), \
                (flen, from, to, rsa, padding)) \
   VMW_SSL_FUNC(crypto, int, RSA_private_decrypt, \
                (int flen, const unsigned char *from, \
                 unsigned char *to, RSA *rsa, int padding), \
                (flen, from, to, rsa, padding)) \
   VMW_SSL_FUNC(crypto, RSA *, RSA_generate_key, \
                (int bits, unsigned long e, \
                 void (*callback)(int, int, void *), void *cb_arg), \
                (bits, e, callback, cb_arg)) \
   VMW_SSL_FUNC(crypto, int, RSA_sign, \
                (int type, const unsigned char *m, unsigned int m_length, \
                 unsigned char *sigret, unsigned int *siglen, RSA *rsa), \
                (type, m, m_length, sigret, siglen, rsa)) \
   VMW_SSL_FUNC(crypto, int, RSA_verify, \
                (int type, const unsigned char *m, unsigned int m_length, \
                 unsigned char *sigbuf, unsigned int siglen, RSA *rsa), \
                (type, m, m_length, sigbuf, siglen, rsa)) \
   VMW_SSL_FUNC(crypto, const EVP_MD *, EVP_sha, (void), ()) \
   VMW_SSL_FUNC(crypto, EVP_PKEY *, EVP_PKEY_new, (void), ()) \
   VMW_SSL_FUNC(crypto, int, i2d_PrivateKey, (EVP_PKEY *a, unsigned char **pp), (a, pp)) \
   VMW_SSL_FUNC(crypto, int, i2d_PublicKey, (EVP_PKEY *a, unsigned char **pp), (a, pp)) \
   VMW_SSL_FUNC(crypto, EVP_PKEY *, d2i_PrivateKey, \
                (int type, EVP_PKEY **a, D2I_PK_T1 pp, long length), \
                (type, a, pp, length)) \
   VMW_SSL_FUNC(crypto, X509 *, d2i_X509, \
                (X509 **px,  D2I_X509_T1 in, long len), (px, in, len)) \
   VMW_SSL_FUNC(crypto, int, EVP_PKEY_size, (EVP_PKEY *pkey), (pkey)) \
   VMW_SSL_FUNC(crypto, int, EVP_PKEY_set1_RSA, (EVP_PKEY *pkey, struct rsa_st *key), \
                (pkey, key)) \
   VMW_SSL_FUNC(crypto, int, EVP_VerifyFinal, \
                (EVP_MD_CTX *ctx, EVP_VF_T1 sigbuf, \
                 unsigned int siglen, EVP_PKEY *pkey), \
                (ctx, sigbuf, siglen, pkey)) \
   VMW_SSL_FUNC(crypto, RSA *, PEM_read_bio_RSAPrivateKey, \
                (BIO *b, RSA **rsa, pem_password_cb *pcb, void *u), \
                (b, rsa, pcb, u)) \
   VMW_SSL_FUNC(crypto, RSA *, PEM_read_bio_RSAPublicKey, \
                (BIO *b, RSA **r, pem_password_cb *pcb, void *u), \
                (b, r, pcb, u)) \
   VMW_SSL_FUNC(crypto, EVP_PKEY *, PEM_read_bio_PUBKEY, \
                (BIO *b, EVP_PKEY **pkey, pem_password_cb *pcb, void *u), \
                (b, pkey, pcb, u)) \
   VMW_SSL_FUNC(crypto, EVP_PKEY *, PEM_read_bio_PrivateKey, \
                (BIO *b, EVP_PKEY **pk, pem_password_cb *pcb, void *u), \
                (b, pk, pcb, u)) \
   VMW_SSL_FUNC(crypto, int, PEM_write_bio_RSAPublicKey, \
                (BIO *bp, PEM_W_B_RPK_T1 rsa), (bp, rsa)) \
   VMW_SSL_FUNC(crypto, int, PEM_write_bio_PKCS8PrivateKey, \
                (BIO *bp, EVP_PKEY *pk, const EVP_CIPHER *c, \
                 char *t, int f, pem_password_cb *pcb, void *u), \
                (bp, pk, c, t, f, pcb, u)) \
   VMW_SSL_FUNC(crypto, int, PEM_write_bio_PUBKEY, \
                (BIO *bp, EVP_PKEY *pk), (bp, pk)) \
   VMW_SSL_FUNC(crypto, int, EVP_CIPHER_CTX_cleanup, (EVP_CIPHER_CTX *a), (a)) \
   VMW_SSL_FUNC(crypto, int,  EVP_EncryptInit, \
                (EVP_CIPHER_CTX *ctx,const EVP_CIPHER *cipher, \
                const unsigned char *key, const unsigned char *iv), \
                (ctx, cipher, key, iv)) \
   VMW_SSL_FUNC(crypto, int, EVP_EncryptUpdate, \
                (EVP_CIPHER_CTX *ctx, unsigned char *out, int *outl, \
                 const unsigned char *in, int inl), (ctx, out, outl, in, inl)) \
   VMW_SSL_FUNC(crypto, int, EVP_EncryptFinal, \
                (EVP_CIPHER_CTX *ctx, unsigned char *out, int *outl), (ctx, out, outl)) \
   VMW_SSL_FUNC(crypto, int, EVP_DecryptInit, \
                (EVP_CIPHER_CTX *ctx,const EVP_CIPHER *cipher, \
                const unsigned char *key, const unsigned char *iv), \
                (ctx, cipher, key, iv)) \
   VMW_SSL_FUNC(crypto, int, EVP_DecryptUpdate, \
                (EVP_CIPHER_CTX *ctx, unsigned char *out, \
                 int *outl, const unsigned char *in, int inl), \
                (ctx, out, outl, in, inl)) \
   VMW_SSL_FUNC(crypto, int, EVP_DecryptFinal, \
                (EVP_CIPHER_CTX *ctx, unsigned char *outm, int *outl), \
                (ctx, outm, outl)) \
   VMW_SSL_FUNC(crypto, const EVP_CIPHER *, EVP_des_cbc, (void), ()) \
   VMW_SSL_FUNC(crypto, const EVP_CIPHER *, EVP_des_ede3_cbc, (void), ()) \
   VMW_SSL_FUNC(crypto, const EVP_CIPHER *, EVP_aes_128_cbc, (void), ()) \
   VMW_SSL_FUNC(ssl, SSL_METHOD *, TLSv1_method, (void), ()) \
   VMW_SSL_FUNC(crypto, size_t, BUF_strlcpy, (char *dst, const char *src, size_t siz), \
                (dst, src, siz)) \
   VMW_SSL_FUNC(crypto, void *, X509_STORE_CTX_get_ex_data, \
                (X509_STORE_CTX *ctx, int idx), (ctx, idx)) \
   VMW_SSL_FUNC(ssl, int, SSL_get_ex_data_X509_STORE_CTX_idx, (void), ()) \
   VMW_SSL_FUNC(ssl, void *, SSL_get_ex_data, (const SSL *ssl, int idx), (ssl, idx)) \
   VMW_SSL_FUNC(ssl, int, SSL_set_ex_data, \
                (SSL *ssl, int idx, void *arg), (ssl, idx, arg)) \
   VMW_SSL_FUNC(crypto, int, i2d_X509, (X509 *x, unsigned char **out), (x, out)) \
   VMW_SSL_FUNC(ssl, int, SSL_get_ex_new_index, \
                (long argl, void *argp, CRYPTO_EX_new *newFunc, \
                 CRYPTO_EX_dup *dup_func, CRYPTO_EX_free *free_func), \
                (argl, argp, newFunc, dup_func, free_func)) \
   VMW_SSL_FUNC(crypto, int, DSA_sign, \
                (int type, const unsigned char *dgst, int len, \
                 unsigned char *sigret, unsigned int *siglen, DSA *dsa), \
                (type, dgst, len, sigret, siglen, dsa)) \
   VMW_SSL_FUNC(crypto, int, DSA_verify, \
                (int type, const unsigned char *dgst, int len, \
                 const unsigned char *sigbuf, int siglen, DSA *dsa), \
                (type, dgst, len, sigbuf, siglen, dsa)) \
   VMW_SSL_FUNC(crypto, int, DSA_generate_key, \
                (DSA *dsa), \
                (dsa)) \
   VMW_SSL_FUNC(crypto, DSA *, d2i_DSAPublicKey, \
                (DSA **a, const unsigned char **pp, long length),  \
                (a, pp, length)) \
   VMW_SSL_FUNC(crypto, DSA *, d2i_DSAPrivateKey, \
                (DSA **a, const unsigned char **pp, long length),  \
                (a, pp, length)) \
   VMW_SSL_FUNC(crypto, int, i2d_DSAPublicKey, \
                (const DSA *a, unsigned char **pp),  \
                (a, pp)) \
   VMW_SSL_FUNC(crypto, int, i2d_DSAPrivateKey, \
                (const DSA *a, unsigned char **pp),  \
                (a, pp)) \
   VMW_SSL_FUNC(crypto, DSA *, DSA_generate_parameters, \
                (int bits, unsigned char *seed, int seed_len, \
                 int *counter_ret, unsigned long *h_ret, \
                 void (*callback)(int, int, void *), void *cb_arg), \
                (bits, seed, seed_len, counter_ret, h_ret, callback, \
                 cb_arg)) \
   VMW_SSL_FUNC(crypto, int, DSA_size, \
                (const DSA *dsa),  \
                (dsa)) \
   VMW_SSL_FUNC(crypto, int, FIPS_mode_set, \
                (int onoff), \
                (onoff)) \
   VMW_SSL_FUNC(crypto, int, FIPS_mode, (void), ())  \
   VMW_SSL_FUNC(crypto, int, DES_set_key, \
                (const_DES_cblock *key, DES_key_schedule *schedule), \
                (key, schedule)) \
   VMW_SSL_FUNC(crypto, unsigned char *, HMAC, \
                (const EVP_MD *evp_md, const void *key, int key_len, \
                 const unsigned char *d, HMAC_T1 n, unsigned char *md, \
                 unsigned int *md_len), \
                (evp_md, key, key_len, d, n, md, md_len)) \
   VMW_SSL_FUNC(crypto, unsigned char *, SHA1, \
                (const unsigned char *d, SHA1_T1 n, unsigned char *md), \
                (d, n, md)) \
   VMW_SSL_FUNC(crypto, unsigned char *, SHA256, \
                (const unsigned char *d, size_t n, unsigned char *md), \
                (d, n, md)) \
   VMW_SSL_FUNC(crypto, unsigned char *, SHA512, \
                (const unsigned char *d, size_t n, unsigned char *md), \
                (d, n, md)) \
   VMW_SSL_FUNC(crypto, const EVP_MD *, EVP_sha1, (void), ()) \
   VMW_SSL_FUNC(crypto, const EVP_MD *, EVP_sha224, (void), ()) \
   VMW_SSL_FUNC(crypto, const EVP_MD *, EVP_sha256, (void), ()) \
   VMW_SSL_FUNC(crypto, const EVP_MD *, EVP_sha384, (void), ()) \
   VMW_SSL_FUNC(crypto, const EVP_MD *, EVP_sha512, (void), ()) \
   VMW_SSL_FUNC(crypto, int, AES_set_encrypt_key, \
                (const unsigned char *userKey, const int bits, \
                 AES_KEY *key), \
                (userKey, bits, key)) \
   VMW_SSL_FUNC(crypto, int, AES_set_decrypt_key, \
                (const unsigned char *userKey, const int bits, \
                 AES_KEY *key), \
                (userKey, bits, key)) \
   VMW_SSL_FUNC(crypto, RSA *, RSA_new, (void), ()) \
   VMW_SSL_FUNC(crypto, RSA *, EVP_PKEY_get1_RSA, (EVP_PKEY *pkey), (pkey)) \
   VMW_SSL_FUNC(crypto, X509 *, X509_new, (void), ()) \
   VMW_SSL_FUNC(crypto, EVP_PKEY *, X509_get_pubkey, (X509 *x), (x)) \
   VMW_SSL_FUNC(crypto, int, EVP_MD_CTX_cleanup, (EVP_MD_CTX *ctx), (ctx)) \
   VMW_SSL_FUNC(crypto, int, EVP_DigestUpdate, (EVP_MD_CTX *ctx, const void *d, EVP_DU_T1 cnt), (ctx, d, cnt)) \
   VMW_SSL_FUNC(crypto, int, EVP_DigestInit, (EVP_MD_CTX *ctx, const EVP_MD *type), (ctx, type)) \
   VMW_SSL_FUNC(crypto, int, EVP_DigestFinal, (EVP_MD_CTX *ctx, unsigned char *md, unsigned int *s), (ctx, md, s)) \
   VMW_SSL_FUNC(crypto, int, EVP_SignFinal, (EVP_MD_CTX *ctx, unsigned char *sig, unsigned int *s, EVP_PKEY *pkey), (ctx, sig, s, pkey)) \
   VMW_SSL_FUNC(crypto, BIGNUM *, BN_bin2bn, (const unsigned char *s,int len,BIGNUM *ret), (s, len, ret)) \
   VMW_SSL_FUNC(crypto, int, BN_bn2bin, (const BIGNUM *a, unsigned char *to), (a, to)) \
   VMW_SSL_FUNC(crypto, int, EVP_DigestFinal_ex, (EVP_MD_CTX *ctx, unsigned char *md, unsigned int *s), (ctx, md, s)) \
   VMW_SSL_FUNC(crypto, int, X509_digest, (const X509 *data, const EVP_MD *type, \
                                           unsigned char *md, unsigned int *len), \
                                          (data, type, md, len)) \
   VMW_SSL_FUNC(crypto, int, X509_NAME_cmp, \
                (const X509_NAME *a, const X509_NAME *b), (a, b))

#ifdef OPENSSL_098

#define VMW_SSL_RET_FUNCTIONS_098 \
   VMW_SSL_FUNC(crypto, const EVP_MD *, EVP_MD_CTX_md, (const EVP_MD_CTX *ctx), (ctx)) \
   VMW_SSL_FUNC(crypto, int, EVP_MD_size, (const EVP_MD *md), (md)) \
   VMW_SSL_FUNC(crypto, int, EVP_CIPHER_iv_length, (const EVP_CIPHER *cipher), (cipher)) \
   VMW_SSL_FUNC(crypto, int, EVP_CIPHER_CTX_block_size, (const EVP_CIPHER_CTX *ctx), (ctx))

#define VMW_SSL_VOID_FUNCTIONS_098 \
   VMW_SSL_FUNC(crypto, void, BIO_set_flags, (BIO *b, int flags), (b, flags)) \
   VMW_SSL_FUNC(crypto, void, BIO_clear_flags, (BIO *b, int flags), (b, flags))
#else
#define VMW_SSL_RET_FUNCTIONS_098
#define VMW_SSL_VOID_FUNCTIONS_098
#endif


/*
 * These allow for libcurl (circa v7.18.0) to be linked statically.
 */

#ifdef OPENSSL_098
#define VMW_SSL_RET_FUNCTIONS_LIBCURL_VERSION_SPECIFIC \
   VMW_SSL_FUNC(ssl, long, SSL_CTX_callback_ctrl, \
                (SSL_CTX *ctx, int cmd, void (*cb)(void)), \
                (ctx, cmd, cb)) \
   VMW_SSL_FUNC(crypto, int, EVP_PKEY_copy_parameters, (EVP_PKEY *to, \
                const EVP_PKEY *from), (to, from)) \
   VMW_SSL_FUNC(crypto, int, MD5_Update, (MD5_CTX *c, const void *data, \
                size_t len), (c, data, len)) \
   VMW_SSL_FUNC(crypto, int, MD4_Update, (MD4_CTX *c, const void *data, \
                size_t len), (c, data, len))
#else
#define VMW_SSL_RET_FUNCTIONS_LIBCURL_VERSION_SPECIFIC \
   VMW_SSL_FUNC(ssl, long, SSL_CTX_callback_ctrl, \
                (SSL_CTX *ctx, int cmd, void (*cb)()), \
                (ctx, cmd, cb)) \
   VMW_SSL_FUNC(crypto, int, EVP_PKEY_copy_parameters, (EVP_PKEY *to, \
                EVP_PKEY *from), (to, from)) \
   VMW_SSL_FUNC(crypto, int, MD5_Update, (MD5_CTX *c, const void *data, \
                unsigned long len), (c, data, len)) \
   VMW_SSL_FUNC(crypto, int, MD4_Update, (MD4_CTX *c, const void *data, \
                unsigned long len), (c, data, len))
#endif

#define VMW_SSL_RET_FUNCTIONS_LIBCURL \
   VMW_SSL_RET_FUNCTIONS_LIBCURL_VERSION_SPECIFIC \
   VMW_SSL_FUNC(ssl, int, SSL_get_shutdown, (const SSL *s), (s)) \
   VMW_SSL_FUNC(ssl, SSL_METHOD *, SSLv23_client_method, (void), ()) \
   VMW_SSL_FUNC(ssl, SSL_METHOD *, SSLv3_client_method, (void), ()) \
   VMW_SSL_FUNC(ssl, SSL_METHOD *, SSLv2_client_method, (void), ()) \
   VMW_SSL_FUNC(ssl, SSL_METHOD *, TLSv1_client_method, (void), ()) \
   VMW_SSL_FUNC(ssl, X509 *, SSL_get_certificate, (const SSL *s), (s)) \
   VMW_SSL_FUNC(ssl, EVP_PKEY *, SSL_get_privatekey, (SSL *s), (s)) \
   VMW_SSL_FUNC(crypto, unsigned long, SSLeay, (void), ()) \
   VMW_SSL_FUNC(crypto, void*, X509_get_ext_d2i, \
                (X509 *x, int nid, int *crit, int *idx), (x, nid, crit, idx)) \
   VMW_SSL_FUNC(crypto, int, sk_num, (const STACK *s), (s)) \
   VMW_SSL_FUNC(crypto, char *, sk_value, (const STACK *s, int k), (s, k)) \
   VMW_SSL_FUNC(crypto, unsigned char *, ASN1_STRING_data, (ASN1_STRING *x), \
                (x)) \
   VMW_SSL_FUNC(crypto, int, ASN1_STRING_length, (ASN1_STRING *x), (x)) \
   VMW_SSL_FUNC(crypto, int, X509_NAME_get_index_by_NID, \
                (X509_NAME *name, int nid, int lastpos), (name, nid, lastpos)) \
   VMW_SSL_FUNC(crypto, X509_NAME_ENTRY *, X509_NAME_get_entry, \
                (X509_NAME *name, int loc), (name, loc)) \
   VMW_SSL_FUNC(crypto, ASN1_STRING *, X509_NAME_ENTRY_get_data, \
                (X509_NAME_ENTRY *ne), (ne)) \
   VMW_SSL_FUNC(crypto, int, ASN1_STRING_type, (ASN1_STRING *x), (x)) \
   VMW_SSL_FUNC(crypto, int, ASN1_STRING_to_UTF8, \
                (unsigned char **out, ASN1_STRING *in), (out, in)) \
   VMW_SSL_FUNC(crypto, void *, CRYPTO_malloc, \
                (int num, const char *file, int line), (num, file, line)) \
   VMW_SSL_FUNC(crypto, int, ENGINE_finish, (ENGINE *e), (e)) \
   VMW_SSL_FUNC(crypto, int, ENGINE_free, (ENGINE *e), (e)) \
   VMW_SSL_FUNC(crypto, ENGINE *, ENGINE_get_first, (void), ()) \
   VMW_SSL_FUNC(crypto, ENGINE *, ENGINE_get_next, (ENGINE *e), (e)) \
   VMW_SSL_FUNC(crypto, const char *, ENGINE_get_id, (const ENGINE *e), (e)) \
   VMW_SSL_FUNC(crypto, int, ENGINE_set_default, \
                (ENGINE *e, unsigned int flags), (e, flags)) \
   VMW_SSL_FUNC(crypto, int, ENGINE_init, (ENGINE *e), (e)) \
   VMW_SSL_FUNC(crypto, EVP_PKEY *, ENGINE_load_private_key, \
                (ENGINE *e, const char *key_id, UI_METHOD *ui_method, \
                 void *callback_data), (e, key_id, ui_method, callback_data)) \
   VMW_SSL_FUNC(crypto, ENGINE *, ENGINE_by_id, (const char *id), (id)) \
   VMW_SSL_FUNC(crypto, int, RAND_egd, (const char *path), (path)) \
   VMW_SSL_FUNC(crypto, const char *, RAND_file_name, (char *file,size_t num), \
                (file, num)) \
   VMW_SSL_FUNC(crypto, UI_METHOD *, UI_OpenSSL, (void), ()) \
   VMW_SSL_FUNC(crypto, int, RAND_bytes, (unsigned char *buf,int num), \
                (buf, num)) \
   VMW_SSL_FUNC(crypto, PKCS12 *, d2i_PKCS12_fp, (FILE *fp, PKCS12 **p12), \
                (fp, p12)) \
   VMW_SSL_FUNC(crypto, int, PKCS12_parse, (PKCS12 *p12, const char *pass, \
                EVP_PKEY **pkey, X509 **cert, STACK **ca), (p12, pass, pkey, \
                cert, ca)) \
   VMW_SSL_FUNC(crypto, int, MD5_Init, (MD5_CTX *c), (c)) \
   VMW_SSL_FUNC(crypto, int, MD5_Final, (unsigned char *md, MD5_CTX *c), \
                (md, c)) \
   VMW_SSL_FUNC(crypto, int, MD4_Init, (MD4_CTX *c), (c)) \
   VMW_SSL_FUNC(crypto, int, MD4_Final, (unsigned char *md, MD4_CTX *c), \
                (md, c))

#define VMW_SSL_VOID_FUNCTIONS_LIBCURL \
   VMW_SSL_FUNC(crypto, void, CRYPTO_free, (void *ctx), (ctx)) \
   VMW_SSL_FUNC(crypto, void, GENERAL_NAMES_free, (GENERAL_NAMES *c), (c)) \
   VMW_SSL_FUNC(crypto, void, ERR_free_strings, (void), ()) \
   VMW_SSL_FUNC(crypto, void, EVP_cleanup, (void), ()) \
   VMW_SSL_FUNC(crypto, void, CRYPTO_cleanup_all_ex_data, (void), ()) \
   VMW_SSL_FUNC(crypto, void, ENGINE_load_builtin_engines, (void), ()) \
   VMW_SSL_FUNC(crypto, void, RAND_add, \
                (const void *buf,int num,double entropy), (buf,num,entropy)) \
   VMW_SSL_FUNC(crypto, void, PKCS12_PBE_add, (void), ()) \
   VMW_SSL_FUNC(crypto, void, PKCS12_free, (PKCS12 *ctx), (ctx)) \
   VMW_SSL_FUNC(crypto, void, DES_set_odd_parity, (DES_cblock *key), (key))


#define VMW_SSL_VOID_FUNCTIONS \
   VMW_SSL_VOID_FUNCTIONS_COMMON \
   VMW_SSL_VOID_FUNCTIONS_098 \
   VMW_SSL_VOID_FUNCTIONS_LIBCURL

#define VMW_SSL_RET_FUNCTIONS \
   VMW_SSL_RET_FUNCTIONS_COMMON \
   VMW_SSL_RET_FUNCTIONS_098 \
   VMW_SSL_RET_FUNCTIONS_LIBCURL

#define VMW_SSL_FUNCTIONS \
   VMW_SSL_RET_FUNCTIONS \
   VMW_SSL_VOID_FUNCTIONS

#endif // ifndef _SSL_FUNCTION_LIST_H_
