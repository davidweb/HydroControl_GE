#include "Crypto.h"
#include <Base64.h>

// La clé PRE_SHARED_KEY du config.h sera utilisée, mais nous devons nous assurer
// qu'elle a la bonne longueur (16, 24 ou 32 octets). Nous la tronquerons ou
// la complèterons si nécessaire.

void generate_aes_key(const String& psk, byte* key_buffer) {
    memset(key_buffer, 0, 16); // Initialiser avec des zéros
    psk.getBytes(key_buffer, 17); // Copier au maximum 16 octets de la clé
}

String CryptoManager::encrypt(const String& plainText, const String& key) {
    byte aes_key[16];
    generate_aes_key(key, aes_key);

    int plainTextLen = plainText.length() + 1;
    char plainTextChar[plainTextLen];
    plainText.toCharArray(plainTextChar, plainTextLen);

    // AES-128-CBC chiffre par blocs de 16 octets.
    // Il faut donc s'assurer que le buffer est un multiple de 16.
    int encryptedLen = aes128_enc_len(plainTextLen);
    byte encrypted[encryptedLen];

    aes128_cbc_encrypt(aes_key, (byte*)aes_iv, (byte*)plainTextChar, plainTextLen, encrypted);

    // Pour la transmission, on encode le résultat en Base64.
    int base64Len = Base64.encodedLength(encryptedLen);
    char base64_buff[base64Len];
    Base64.encode(base64_buff, (char*)encrypted, encryptedLen);

    return String(base64_buff);
}

String CryptoManager::decrypt(const String& encryptedBase64, const String& key) {
    byte aes_key[16];
    generate_aes_key(key, aes_key);

    int encryptedBase64Len = encryptedBase64.length() + 1;
    char encryptedBase64Char[encryptedBase64Len];
    encryptedBase64.toCharArray(encryptedBase64Char, encryptedBase64Len);

    // Décoder depuis Base64
    int decodedLen = Base64.decodedLength(encryptedBase64Char, encryptedBase64Len);
    byte decoded[decodedLen];
    Base64.decode((char*)decoded, encryptedBase64Char, encryptedBase64Len);

    // Déchiffrer
    // Le buffer de sortie doit avoir la même taille que le buffer chiffré.
    char decrypted[decodedLen];
    aes128_cbc_decrypt(aes_key, (byte*)aes_iv, decoded, decodedLen, (byte*)decrypted);

    // La chaîne déchiffrée contient du padding qui doit être ignoré.
    // La bibliothèque AESLib ajoute un padding PKCS7, mais ne fournit pas de fonction
    // pour le retirer. Le C-string est terminé par un null, donc on peut le lire directement.
    return String(decrypted);
}
