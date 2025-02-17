/************************************************************************************
*   written by: @Bitmaker
*   optimized by: @matteocrippa
*   based on: Blockstream Jade shaLib
*   thanks to @LarryBitcoin

*   Description:

*   NerdSha256plus is a custom C implementation of sha256d based on Blockstream Jade
    code https://github.com/Blockstream/Jade

    The following file can be used on any ESP32 implementation using both cores

*************************************************************************************/

#include "nerdSHA256plus.h"

/**
 * – Um array de 64 constantes de 32 bits (K[64]).
 * – Esses valores são os “números mágicos” definidos pelo padrão SHA‑256 e são usados em 
 * cada uma das 64 rodadas do algoritmo de compressão.
 */
MEM_ATTR static const uint32_t K[64] = {
    0x428A2F98L, 0x71374491L, 0xB5C0FBCFL, 0xE9B5DBA5L, 0x3956C25BL,
    0x59F111F1L, 0x923F82A4L, 0xAB1C5ED5L, 0xD807AA98L, 0x12835B01L,
    0x243185BEL, 0x550C7DC3L, 0x72BE5D74L, 0x80DEB1FEL, 0x9BDC06A7L,
    0xC19BF174L, 0xE49B69C1L, 0xEFBE4786L, 0x0FC19DC6L, 0x240CA1CCL,
    0x2DE92C6FL, 0x4A7484AAL, 0x5CB0A9DCL, 0x76F988DAL, 0x983E5152L,
    0xA831C66DL, 0xB00327C8L, 0xBF597FC7L, 0xC6E00BF3L, 0xD5A79147L,
    0x06CA6351L, 0x14292967L, 0x27B70A85L, 0x2E1B2138L, 0x4D2C6DFCL,
    0x53380D13L, 0x650A7354L, 0x766A0ABBL, 0x81C2C92EL, 0x92722C85L,
    0xA2BFE8A1L, 0xA81A664BL, 0xC24B8B70L, 0xC76C51A3L, 0xD192E819L,
    0xD6990624L, 0xF40E3585L, 0x106AA070L, 0x19A4C116L, 0x1E376C08L,
    0x2748774CL, 0x34B0BCB5L, 0x391C0CB3L, 0x4ED8AA4AL, 0x5B9CCA4FL,
    0x682E6FF3L, 0x748F82EEL, 0x78A5636FL, 0x84C87814L, 0x8CC70208L,
    0x90BEFFFAL, 0xA4506CEBL, 0xBEF9A3F7L, 0xC67178F2L};

/**
 * Realiza uma rotação à direita de um número de 32 bits. 
 * Essa operação é fundamental no SHA‑256.
 * 
 * A rotação à direita é uma operação de manipulação de bits que "gira" os bits de um número 
 * para a direita, de forma circular. Isso significa que, quando você desloca todos os bits 
 * para a direita, os bits que "saem" pela extremidade direita são reinseridos na extremidade 
 * esquerda.
 * 
 * Por exemplo, suponha um número de 8 bits representado como:
 * 
 *   A B C D E F G H
 * 
 * Se realizarmos uma rotação à direita de 3 posições, os três bits mais à direita (F, G, H) 
 * serão movidos para a esquerda, e o restante será deslocado para a direita. O resultado 
 * será:
 *   F G H A B C D E
 * 
 * Diferente de um shift lógico, em que os bits que saem são descartados e zeros são inseridos,
 * a rotação mantém todos os bits do número, apenas alterando sua posição. Essa operação é 
 * fundamental no SHA‑256 porque ela ajuda a misturar os bits de maneira não-linear, 
 * contribuindo para o efeito avalanche (ou seja, pequenas mudanças na entrada geram 
 * grandes mudanças na saída).
 * 
 * Vamos detalhar a expressão:
 * 
 *   x << ((sizeof(x) << 3) - n)
 * 
 * Passo a passo:
 * 
 *     sizeof(x):
 *       – Retorna o tamanho de x em bytes.
 *       – Para um uint32_t, sizeof(x) é 4.
 * 
 *     (sizeof(x) << 3):
 *       – O operador << 3 desloca à esquerda o valor de sizeof(x) por 3 bits.
 *       – Deslocar um número à esquerda por 3 bits é equivalente a multiplicar esse número por 2³, ou seja, por 8.
 *       – Então, (4 << 3) equivale a 4 * 8 = 32.
 *       – Esse valor (32) representa o número total de bits em um uint32_t.
 * 
 *     ((sizeof(x) << 3) - n):
 *       – Aqui, subtraímos n (o número de bits que queremos rotacionar) de 32.
 *       – Por exemplo, se n for 8, teremos 32 - 8 = 24.
 * 
 *     x << ((sizeof(x) << 3) - n):
 *       – Agora, deslocamos o valor de x para a esquerda por (32 - n) bits.
 *       – Esse deslocamento move os bits que “saíram” do lado direito quando fizemos x >> n de volta para as posições mais à esquerda.
 *       – No exemplo com n = 8, x << 24 desloca x para a esquerda por 24 bits, de modo que os 8 bits menos significativos de x (que seriam "perdidos" no shift direito) acabam voltando para as posições mais altas.
 * 
 * Em resumo, essa parte da função ROTR pega os bits que foram "deslocados para fora" na 
 * operação x >> n e os recoloca nas posições de maior peso, completando a rotação de forma 
 * circular. Assim, a rotação à direita é implementada como a combinação de:
 * 
 *     x >> n  – desloca os bits para a direita, e
 *     x << (total_bits - n) – recoloca os bits que saíram para a esquerda.
 * 
 * Por fim, o operador OR (|) junta os dois resultados, obtendo a rotação completa.
 */
inline uint32_t ROTR(uint32_t x, uint32_t n)
{
    return ((x >> n) | (x << ((sizeof(x) << 3) - n)));
}

/**
 *  Essa função serve para converter um número de 32 bits em sua representação em bytes 
 * (big-endian) e armazená-los em um array. Vamos detalhar linha por linha:
 *     Parâmetros:
 *      – n é um número de 32 bits que queremos "quebrar" em 4 bytes.
 *      – b é um ponteiro para um array de bytes onde os resultados serão armazenados.
 *      – i é o índice no array b onde começará a escrita dos 4 bytes.
 * 
 *     Linha 1:
 *       (b)[(i)] = (uint8_t)((n) >> 24);
 *      – Aqui, deslocamos o número n 24 bits para a direita.
 *      – Isso extrai o byte mais significativo (os 8 bits mais à esquerda) de n.
 *      – Em seguida, convertemos para um uint8_t (garantindo que só os 8 bits relevantes sejam usados) e armazenamos em b[i].
 * 
 *     Linha 2:
 *       (b)[(i) + 1] = (uint8_t)((n) >> 16);
 *      – Agora, deslocamos n 16 bits para a direita.
 *      – Isso traz para os 8 bits menos significativos os 2º byte mais significativo de n.
 *      – O resultado é armazenado em b[i + 1].
 * 
 *     Linha 3:
 *       (b)[(i) + 2] = (uint8_t)((n) >> 8);
 *      – Deslocamos n 8 bits para a direita, fazendo com que o 3º byte mais significativo fique nos 8 bits menos significativos.
 *      – Armazenamos esse valor em b[i + 2].
 * 
 *     Linha 4:
 *       (b)[(i) + 3] = (uint8_t)((n));
 *      – Aqui, não há deslocamento: pegamos os 8 bits menos significativos de n (o 4º byte) e armazenamos em b[i + 3].
 * 
 * Em resumo:
 *  A função divide o número n em 4 bytes, de forma que:
 *     b[i] recebe o byte mais significativo (parte alta do número).
 *     b[i+1] recebe o segundo byte.
 *     b[i+2] recebe o terceiro byte.
 *     b[i+3] recebe o byte menos significativo (parte baixa).
 * 
 * Isso é conhecido como representação big-endian, onde o byte mais significativo vem primeiro
 * (endereço de memória mais baixo). Essa conversão é fundamental em muitas operações de 
 * criptografia e protocolos de rede, onde a ordem dos bytes precisa ser padronizada.
 */
inline void PUT_UINT32_BE(uint32_t n, uint8_t *b, uint32_t i)
{
    (b)[(i)] = (uint8_t)((n) >> 24);
    (b)[(i) + 1] = (uint8_t)((n) >> 16);
    (b)[(i) + 2] = (uint8_t)((n) >> 8);
    (b)[(i) + 3] = (uint8_t)((n));
}

/**
 * Pega um array de valores em big-endian e unificar num valor final
 * 
 * Exemplo Prático
 * Se o array b contém os seguintes bytes:
 *     b[i] = 0x12
 *     b[i+1] = 0x34
 *     b[i+2] = 0x56
 *     b[i+3] = 0x78
 * 
 * A função fará o seguinte:
 * 
 *     (uint32_t)(0x12) << 24 → 0x12000000
 *     (uint32_t)(0x34) << 16 → 0x00340000
 *     (uint32_t)(0x56) << 8 → 0x00005600
 *     (uint32_t)(0x78) → 0x00000078
 * 
 * Quando esses valores são combinados com OR:
 * 
 *   0x12000000 | 0x00340000 | 0x00005600 | 0x00000078 = 0x12345678
 * 
 * Portanto, a função retorna 0x12345678, que é o número de 32 bits reconstruído a partir 
 * dos 4 bytes em ordem big-endian.
 */
inline uint32_t GET_UINT32_BE(const uint8_t *b, uint32_t i)
{
    return (((uint32_t)(b)[(i)] << 24) | ((uint32_t)(b)[(i) + 1] << 16) | ((uint32_t)(b)[(i) + 2] << 8) | ((uint32_t)(b)[(i) + 3]));
}

/**
 * Realiza uma operação de deslocamento lógico à direita (shift right).
 * 
 * Exemplo Prático
 * 
 * Considere:
 * 
 *   x = 0x12345678
 *   n = 4
 * 
 * Vamos aplicar a operação:
 * 
 *     Conversão e Garantia de 32 bits:
 *       (x & 0xFFFFFFFF) mantém o valor 0x12345678, já que x já tem 32 bits.
 * 
 *     Deslocamento à direita (x >> n):
 *       Deslocar 0x12345678 4 bits à direita equivale a dividir o valor por 2⁴ (ou 16) e descartar os bits "sobrando" à direita. Em termos hexadecimais, deslocar 4 bits equivale a "mover" cada dígito hexadecimal para a direita por uma posição.
 * 
 *         O valor original em hexadecimal é:
 *            0x12 34 56 78
 * 
 *         Deslocando 4 bits (1 dígito hexadecimal) à direita, obtemos:
 *            0x01 23 45 67

    Resultado:
      Portanto, SHR(0x12345678, 4) retorna 0x01234567.
 */
inline uint32_t SHR(uint32_t x, uint32_t n)
{
    return ((x & 0xFFFFFFFF) >> n);
}

/**
 * S0, S1, S2, S3: Implementam as funções de “sigma” definidas pelo padrão SHA‑256. 
 * Estas funções realizam rotações e deslocamentos e são cruciais para a “difusão” 
 * dos bits durante a compressão.
 */
inline uint32_t S0(uint32_t x)
{
    return (ROTR(x, 7) ^ ROTR(x, 18) ^ SHR(x, 3));
}
inline uint32_t S1(uint32_t x)
{
    return (ROTR(x, 17) ^ ROTR(x, 19) ^ SHR(x, 10));
}
inline uint32_t S2(uint32_t x)
{
    return (ROTR(x, 2) ^ ROTR(x, 13) ^ ROTR(x, 22));
}
inline uint32_t S3(uint32_t x)
{
    return (ROTR(x, 6) ^ ROTR(x, 11) ^ ROTR(x, 25));
}

/**
 * F0 e F1: São macros que implementam funções de escolha (choose) e maioria (majority) – funções 
 * não lineares que combinam os bits dos estados intermediários de forma a introduzir não 
 * linearidade, uma característica essencial em funções hash criptográficas.
 */
#define F0(x, y, z) ((x & y) | (z & (x | y)))
#define F1(x, y, z) (z ^ (x & (y ^ z)))

/**
 * Macro R: É usada para estender o bloco de 16 palavras (inicialmente carregadas a partir do 
 * bloco de entrada) para as 64 palavras necessárias nas 64 rodadas do SHA‑256.
 */
#define R(t) (W[t] = S1(W[t - 2]) + W[t - 7] + S0(W[t - 15]) + W[t - 16])

/**
 * Representa uma rodada de compressão do SHA‑256. Ela combina os valores dos oito registradores 
 * (A, B, C, D, E, F, G, H) com uma palavra do bloco expandido e uma constante K, realizando 
 * as operações de rotação, soma e funções não lineares conforme o padrão do SHA‑256.
 */
#define P(a, b, c, d, e, f, g, h, x, K)          \
    {                                            \
        temp1 = h + S3(e) + F1(e, f, g) + K + x; \
        temp2 = S2(a) + F0(a, b, c);             \
        d += temp1;                              \
        h = temp1 + temp2;                       \
    }

RAM_ATTR void nerd_mids(nerdSHA256_context *midstate, uint8_t dataIn[NERD_SHA256_BLOCK_SIZE])
{
    uint32_t A[8] = {0x6A09E667, 0xBB67AE85, 0x3C6EF372, 0xA54FF53A, 0x510E527F, 0x9B05688C, 0x1F83D9AB, 0x5BE0CD19};

    uint32_t temp1, temp2, W[64];

    W[0] = GET_UINT32_BE(dataIn, 0);
    W[1] = GET_UINT32_BE(dataIn, 4);
    W[2] = GET_UINT32_BE(dataIn, 8);
    W[3] = GET_UINT32_BE(dataIn, 12);
    W[4] = GET_UINT32_BE(dataIn, 16);
    W[5] = GET_UINT32_BE(dataIn, 20);
    W[6] = GET_UINT32_BE(dataIn, 24);
    W[7] = GET_UINT32_BE(dataIn, 28);
    W[8] = GET_UINT32_BE(dataIn, 32);
    W[9] = GET_UINT32_BE(dataIn, 36);
    W[10] = GET_UINT32_BE(dataIn, 40);
    W[11] = GET_UINT32_BE(dataIn, 44);
    W[12] = GET_UINT32_BE(dataIn, 48);
    W[13] = GET_UINT32_BE(dataIn, 52);
    W[14] = GET_UINT32_BE(dataIn, 56);
    W[15] = GET_UINT32_BE(dataIn, 60);

    P(A[0], A[1], A[2], A[3], A[4], A[5], A[6], A[7], W[0], K[0]);
    P(A[7], A[0], A[1], A[2], A[3], A[4], A[5], A[6], W[1], K[1]);
    P(A[6], A[7], A[0], A[1], A[2], A[3], A[4], A[5], W[2], K[2]);
    P(A[5], A[6], A[7], A[0], A[1], A[2], A[3], A[4], W[3], K[3]);
    P(A[4], A[5], A[6], A[7], A[0], A[1], A[2], A[3], W[4], K[4]);
    P(A[3], A[4], A[5], A[6], A[7], A[0], A[1], A[2], W[5], K[5]);
    P(A[2], A[3], A[4], A[5], A[6], A[7], A[0], A[1], W[6], K[6]);
    P(A[1], A[2], A[3], A[4], A[5], A[6], A[7], A[0], W[7], K[7]);
    P(A[0], A[1], A[2], A[3], A[4], A[5], A[6], A[7], W[8], K[8]);
    P(A[7], A[0], A[1], A[2], A[3], A[4], A[5], A[6], W[9], K[9]);
    P(A[6], A[7], A[0], A[1], A[2], A[3], A[4], A[5], W[10], K[10]);
    P(A[5], A[6], A[7], A[0], A[1], A[2], A[3], A[4], W[11], K[11]);
    P(A[4], A[5], A[6], A[7], A[0], A[1], A[2], A[3], W[12], K[12]);
    P(A[3], A[4], A[5], A[6], A[7], A[0], A[1], A[2], W[13], K[13]);
    P(A[2], A[3], A[4], A[5], A[6], A[7], A[0], A[1], W[14], K[14]);
    P(A[1], A[2], A[3], A[4], A[5], A[6], A[7], A[0], W[15], K[15]);
    P(A[0], A[1], A[2], A[3], A[4], A[5], A[6], A[7], R(16), K[16]);
    P(A[7], A[0], A[1], A[2], A[3], A[4], A[5], A[6], R(17), K[17]);
    P(A[6], A[7], A[0], A[1], A[2], A[3], A[4], A[5], R(18), K[18]);
    P(A[5], A[6], A[7], A[0], A[1], A[2], A[3], A[4], R(19), K[19]);
    P(A[4], A[5], A[6], A[7], A[0], A[1], A[2], A[3], R(20), K[20]);
    P(A[3], A[4], A[5], A[6], A[7], A[0], A[1], A[2], R(21), K[21]);
    P(A[2], A[3], A[4], A[5], A[6], A[7], A[0], A[1], R(22), K[22]);
    P(A[1], A[2], A[3], A[4], A[5], A[6], A[7], A[0], R(23), K[23]);
    P(A[0], A[1], A[2], A[3], A[4], A[5], A[6], A[7], R(24), K[24]);
    P(A[7], A[0], A[1], A[2], A[3], A[4], A[5], A[6], R(25), K[25]);
    P(A[6], A[7], A[0], A[1], A[2], A[3], A[4], A[5], R(26), K[26]);
    P(A[5], A[6], A[7], A[0], A[1], A[2], A[3], A[4], R(27), K[27]);
    P(A[4], A[5], A[6], A[7], A[0], A[1], A[2], A[3], R(28), K[28]);
    P(A[3], A[4], A[5], A[6], A[7], A[0], A[1], A[2], R(29), K[29]);
    P(A[2], A[3], A[4], A[5], A[6], A[7], A[0], A[1], R(30), K[30]);
    P(A[1], A[2], A[3], A[4], A[5], A[6], A[7], A[0], R(31), K[31]);
    P(A[0], A[1], A[2], A[3], A[4], A[5], A[6], A[7], R(32), K[32]);
    P(A[7], A[0], A[1], A[2], A[3], A[4], A[5], A[6], R(33), K[33]);
    P(A[6], A[7], A[0], A[1], A[2], A[3], A[4], A[5], R(34), K[34]);
    P(A[5], A[6], A[7], A[0], A[1], A[2], A[3], A[4], R(35), K[35]);
    P(A[4], A[5], A[6], A[7], A[0], A[1], A[2], A[3], R(36), K[36]);
    P(A[3], A[4], A[5], A[6], A[7], A[0], A[1], A[2], R(37), K[37]);
    P(A[2], A[3], A[4], A[5], A[6], A[7], A[0], A[1], R(38), K[38]);
    P(A[1], A[2], A[3], A[4], A[5], A[6], A[7], A[0], R(39), K[39]);
    P(A[0], A[1], A[2], A[3], A[4], A[5], A[6], A[7], R(40), K[40]);
    P(A[7], A[0], A[1], A[2], A[3], A[4], A[5], A[6], R(41), K[41]);
    P(A[6], A[7], A[0], A[1], A[2], A[3], A[4], A[5], R(42), K[42]);
    P(A[5], A[6], A[7], A[0], A[1], A[2], A[3], A[4], R(43), K[43]);
    P(A[4], A[5], A[6], A[7], A[0], A[1], A[2], A[3], R(44), K[44]);
    P(A[3], A[4], A[5], A[6], A[7], A[0], A[1], A[2], R(45), K[45]);
    P(A[2], A[3], A[4], A[5], A[6], A[7], A[0], A[1], R(46), K[46]);
    P(A[1], A[2], A[3], A[4], A[5], A[6], A[7], A[0], R(47), K[47]);
    P(A[0], A[1], A[2], A[3], A[4], A[5], A[6], A[7], R(48), K[48]);
    P(A[7], A[0], A[1], A[2], A[3], A[4], A[5], A[6], R(49), K[49]);
    P(A[6], A[7], A[0], A[1], A[2], A[3], A[4], A[5], R(50), K[50]);
    P(A[5], A[6], A[7], A[0], A[1], A[2], A[3], A[4], R(51), K[51]);
    P(A[4], A[5], A[6], A[7], A[0], A[1], A[2], A[3], R(52), K[52]);
    P(A[3], A[4], A[5], A[6], A[7], A[0], A[1], A[2], R(53), K[53]);
    P(A[2], A[3], A[4], A[5], A[6], A[7], A[0], A[1], R(54), K[54]);
    P(A[1], A[2], A[3], A[4], A[5], A[6], A[7], A[0], R(55), K[55]);
    P(A[0], A[1], A[2], A[3], A[4], A[5], A[6], A[7], R(56), K[56]);
    P(A[7], A[0], A[1], A[2], A[3], A[4], A[5], A[6], R(57), K[57]);
    P(A[6], A[7], A[0], A[1], A[2], A[3], A[4], A[5], R(58), K[58]);
    P(A[5], A[6], A[7], A[0], A[1], A[2], A[3], A[4], R(59), K[59]);
    P(A[4], A[5], A[6], A[7], A[0], A[1], A[2], A[3], R(60), K[60]);
    P(A[3], A[4], A[5], A[6], A[7], A[0], A[1], A[2], R(61), K[61]);
    P(A[2], A[3], A[4], A[5], A[6], A[7], A[0], A[1], R(62), K[62]);
    P(A[1], A[2], A[3], A[4], A[5], A[6], A[7], A[0], R(63), K[63]);

    midstate->digest[0] = 0x6A09E667 + A[0];
    midstate->digest[1] = 0xBB67AE85 + A[1];
    midstate->digest[2] = 0x3C6EF372 + A[2];
    midstate->digest[3] = 0xA54FF53A + A[3];
    midstate->digest[4] = 0x510E527F + A[4];
    midstate->digest[5] = 0x9B05688C + A[5];
    midstate->digest[6] = 0x1F83D9AB + A[6];
    midstate->digest[7] = 0x5BE0CD19 + A[7];
}

RAM_ATTR uint8_t nerd_sha256d(nerdSHA256_context *midstate, uint8_t dataIn[NERD_JOB_BLOCK_SIZE], uint8_t doubleHash[NERD_SHA256_BLOCK_SIZE])
{
    uint32_t temp1, temp2;

    //*********** Init 1rst SHA ***********

    uint32_t W[64] = {GET_UINT32_BE(dataIn, 0), GET_UINT32_BE(dataIn, 4),
                      GET_UINT32_BE(dataIn, 8), GET_UINT32_BE(dataIn, 12), 0x80000000, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                      0, 640};

    uint32_t A[8] = {midstate->digest[0], midstate->digest[1], midstate->digest[2], midstate->digest[3],
                     midstate->digest[4], midstate->digest[5], midstate->digest[6], midstate->digest[7]};

    P(A[0], A[1], A[2], A[3], A[4], A[5], A[6], A[7], W[0], K[0]);
    P(A[7], A[0], A[1], A[2], A[3], A[4], A[5], A[6], W[1], K[1]);
    P(A[6], A[7], A[0], A[1], A[2], A[3], A[4], A[5], W[2], K[2]);
    P(A[5], A[6], A[7], A[0], A[1], A[2], A[3], A[4], W[3], K[3]);
    P(A[4], A[5], A[6], A[7], A[0], A[1], A[2], A[3], W[4], K[4]);
    P(A[3], A[4], A[5], A[6], A[7], A[0], A[1], A[2], W[5], K[5]);
    P(A[2], A[3], A[4], A[5], A[6], A[7], A[0], A[1], W[6], K[6]);
    P(A[1], A[2], A[3], A[4], A[5], A[6], A[7], A[0], W[7], K[7]);
    P(A[0], A[1], A[2], A[3], A[4], A[5], A[6], A[7], W[8], K[8]);
    P(A[7], A[0], A[1], A[2], A[3], A[4], A[5], A[6], W[9], K[9]);
    P(A[6], A[7], A[0], A[1], A[2], A[3], A[4], A[5], W[10], K[10]);
    P(A[5], A[6], A[7], A[0], A[1], A[2], A[3], A[4], W[11], K[11]);
    P(A[4], A[5], A[6], A[7], A[0], A[1], A[2], A[3], W[12], K[12]);
    P(A[3], A[4], A[5], A[6], A[7], A[0], A[1], A[2], W[13], K[13]);
    P(A[2], A[3], A[4], A[5], A[6], A[7], A[0], A[1], W[14], K[14]);
    P(A[1], A[2], A[3], A[4], A[5], A[6], A[7], A[0], W[15], K[15]);
    P(A[0], A[1], A[2], A[3], A[4], A[5], A[6], A[7], R(16), K[16]);
    P(A[7], A[0], A[1], A[2], A[3], A[4], A[5], A[6], R(17), K[17]);
    P(A[6], A[7], A[0], A[1], A[2], A[3], A[4], A[5], R(18), K[18]);
    P(A[5], A[6], A[7], A[0], A[1], A[2], A[3], A[4], R(19), K[19]);
    P(A[4], A[5], A[6], A[7], A[0], A[1], A[2], A[3], R(20), K[20]);
    P(A[3], A[4], A[5], A[6], A[7], A[0], A[1], A[2], R(21), K[21]);
    P(A[2], A[3], A[4], A[5], A[6], A[7], A[0], A[1], R(22), K[22]);
    P(A[1], A[2], A[3], A[4], A[5], A[6], A[7], A[0], R(23), K[23]);
    P(A[0], A[1], A[2], A[3], A[4], A[5], A[6], A[7], R(24), K[24]);
    P(A[7], A[0], A[1], A[2], A[3], A[4], A[5], A[6], R(25), K[25]);
    P(A[6], A[7], A[0], A[1], A[2], A[3], A[4], A[5], R(26), K[26]);
    P(A[5], A[6], A[7], A[0], A[1], A[2], A[3], A[4], R(27), K[27]);
    P(A[4], A[5], A[6], A[7], A[0], A[1], A[2], A[3], R(28), K[28]);
    P(A[3], A[4], A[5], A[6], A[7], A[0], A[1], A[2], R(29), K[29]);
    P(A[2], A[3], A[4], A[5], A[6], A[7], A[0], A[1], R(30), K[30]);
    P(A[1], A[2], A[3], A[4], A[5], A[6], A[7], A[0], R(31), K[31]);
    P(A[0], A[1], A[2], A[3], A[4], A[5], A[6], A[7], R(32), K[32]);
    P(A[7], A[0], A[1], A[2], A[3], A[4], A[5], A[6], R(33), K[33]);
    P(A[6], A[7], A[0], A[1], A[2], A[3], A[4], A[5], R(34), K[34]);
    P(A[5], A[6], A[7], A[0], A[1], A[2], A[3], A[4], R(35), K[35]);
    P(A[4], A[5], A[6], A[7], A[0], A[1], A[2], A[3], R(36), K[36]);
    P(A[3], A[4], A[5], A[6], A[7], A[0], A[1], A[2], R(37), K[37]);
    P(A[2], A[3], A[4], A[5], A[6], A[7], A[0], A[1], R(38), K[38]);
    P(A[1], A[2], A[3], A[4], A[5], A[6], A[7], A[0], R(39), K[39]);
    P(A[0], A[1], A[2], A[3], A[4], A[5], A[6], A[7], R(40), K[40]);
    P(A[7], A[0], A[1], A[2], A[3], A[4], A[5], A[6], R(41), K[41]);
    P(A[6], A[7], A[0], A[1], A[2], A[3], A[4], A[5], R(42), K[42]);
    P(A[5], A[6], A[7], A[0], A[1], A[2], A[3], A[4], R(43), K[43]);
    P(A[4], A[5], A[6], A[7], A[0], A[1], A[2], A[3], R(44), K[44]);
    P(A[3], A[4], A[5], A[6], A[7], A[0], A[1], A[2], R(45), K[45]);
    P(A[2], A[3], A[4], A[5], A[6], A[7], A[0], A[1], R(46), K[46]);
    P(A[1], A[2], A[3], A[4], A[5], A[6], A[7], A[0], R(47), K[47]);
    P(A[0], A[1], A[2], A[3], A[4], A[5], A[6], A[7], R(48), K[48]);
    P(A[7], A[0], A[1], A[2], A[3], A[4], A[5], A[6], R(49), K[49]);
    P(A[6], A[7], A[0], A[1], A[2], A[3], A[4], A[5], R(50), K[50]);
    P(A[5], A[6], A[7], A[0], A[1], A[2], A[3], A[4], R(51), K[51]);
    P(A[4], A[5], A[6], A[7], A[0], A[1], A[2], A[3], R(52), K[52]);
    P(A[3], A[4], A[5], A[6], A[7], A[0], A[1], A[2], R(53), K[53]);
    P(A[2], A[3], A[4], A[5], A[6], A[7], A[0], A[1], R(54), K[54]);
    P(A[1], A[2], A[3], A[4], A[5], A[6], A[7], A[0], R(55), K[55]);
    P(A[0], A[1], A[2], A[3], A[4], A[5], A[6], A[7], R(56), K[56]);
    P(A[7], A[0], A[1], A[2], A[3], A[4], A[5], A[6], R(57), K[57]);
    P(A[6], A[7], A[0], A[1], A[2], A[3], A[4], A[5], R(58), K[58]);
    P(A[5], A[6], A[7], A[0], A[1], A[2], A[3], A[4], R(59), K[59]);
    P(A[4], A[5], A[6], A[7], A[0], A[1], A[2], A[3], R(60), K[60]);
    P(A[3], A[4], A[5], A[6], A[7], A[0], A[1], A[2], R(61), K[61]);
    P(A[2], A[3], A[4], A[5], A[6], A[7], A[0], A[1], R(62), K[62]);
    P(A[1], A[2], A[3], A[4], A[5], A[6], A[7], A[0], R(63), K[63]);

    //*********** end SHA_finish ***********

    /* Calculate the second hash (double SHA-256) */

    W[0] = A[0] + midstate->digest[0];
    W[1] = A[1] + midstate->digest[1];
    W[2] = A[2] + midstate->digest[2];
    W[3] = A[3] + midstate->digest[3];
    W[4] = A[4] + midstate->digest[4];
    W[5] = A[5] + midstate->digest[5];
    W[6] = A[6] + midstate->digest[6];
    W[7] = A[7] + midstate->digest[7];
    W[8] = 0x80000000;
    W[9] = 0;
    W[10] = 0;
    W[11] = 0;
    W[12] = 0;
    W[13] = 0;
    W[14] = 0;
    W[15] = 256;

    A[0] = 0x6A09E667;
    A[1] = 0xBB67AE85;
    A[2] = 0x3C6EF372;
    A[3] = 0xA54FF53A;
    A[4] = 0x510E527F;
    A[5] = 0x9B05688C;
    A[6] = 0x1F83D9AB;
    A[7] = 0x5BE0CD19;

    P(A[0], A[1], A[2], A[3], A[4], A[5], A[6], A[7], W[0], K[0]);
    P(A[7], A[0], A[1], A[2], A[3], A[4], A[5], A[6], W[1], K[1]);
    P(A[6], A[7], A[0], A[1], A[2], A[3], A[4], A[5], W[2], K[2]);
    P(A[5], A[6], A[7], A[0], A[1], A[2], A[3], A[4], W[3], K[3]);
    P(A[4], A[5], A[6], A[7], A[0], A[1], A[2], A[3], W[4], K[4]);
    P(A[3], A[4], A[5], A[6], A[7], A[0], A[1], A[2], W[5], K[5]);
    P(A[2], A[3], A[4], A[5], A[6], A[7], A[0], A[1], W[6], K[6]);
    P(A[1], A[2], A[3], A[4], A[5], A[6], A[7], A[0], W[7], K[7]);
    P(A[0], A[1], A[2], A[3], A[4], A[5], A[6], A[7], W[8], K[8]);
    P(A[7], A[0], A[1], A[2], A[3], A[4], A[5], A[6], W[9], K[9]);
    P(A[6], A[7], A[0], A[1], A[2], A[3], A[4], A[5], W[10], K[10]);
    P(A[5], A[6], A[7], A[0], A[1], A[2], A[3], A[4], W[11], K[11]);
    P(A[4], A[5], A[6], A[7], A[0], A[1], A[2], A[3], W[12], K[12]);
    P(A[3], A[4], A[5], A[6], A[7], A[0], A[1], A[2], W[13], K[13]);
    P(A[2], A[3], A[4], A[5], A[6], A[7], A[0], A[1], W[14], K[14]);
    P(A[1], A[2], A[3], A[4], A[5], A[6], A[7], A[0], W[15], K[15]);
    P(A[0], A[1], A[2], A[3], A[4], A[5], A[6], A[7], R(16), K[16]);
    P(A[7], A[0], A[1], A[2], A[3], A[4], A[5], A[6], R(17), K[17]);
    P(A[6], A[7], A[0], A[1], A[2], A[3], A[4], A[5], R(18), K[18]);
    P(A[5], A[6], A[7], A[0], A[1], A[2], A[3], A[4], R(19), K[19]);
    P(A[4], A[5], A[6], A[7], A[0], A[1], A[2], A[3], R(20), K[20]);
    P(A[3], A[4], A[5], A[6], A[7], A[0], A[1], A[2], R(21), K[21]);
    P(A[2], A[3], A[4], A[5], A[6], A[7], A[0], A[1], R(22), K[22]);
    P(A[1], A[2], A[3], A[4], A[5], A[6], A[7], A[0], R(23), K[23]);
    P(A[0], A[1], A[2], A[3], A[4], A[5], A[6], A[7], R(24), K[24]);
    P(A[7], A[0], A[1], A[2], A[3], A[4], A[5], A[6], R(25), K[25]);
    P(A[6], A[7], A[0], A[1], A[2], A[3], A[4], A[5], R(26), K[26]);
    P(A[5], A[6], A[7], A[0], A[1], A[2], A[3], A[4], R(27), K[27]);
    P(A[4], A[5], A[6], A[7], A[0], A[1], A[2], A[3], R(28), K[28]);
    P(A[3], A[4], A[5], A[6], A[7], A[0], A[1], A[2], R(29), K[29]);
    P(A[2], A[3], A[4], A[5], A[6], A[7], A[0], A[1], R(30), K[30]);
    P(A[1], A[2], A[3], A[4], A[5], A[6], A[7], A[0], R(31), K[31]);
    P(A[0], A[1], A[2], A[3], A[4], A[5], A[6], A[7], R(32), K[32]);
    P(A[7], A[0], A[1], A[2], A[3], A[4], A[5], A[6], R(33), K[33]);
    P(A[6], A[7], A[0], A[1], A[2], A[3], A[4], A[5], R(34), K[34]);
    P(A[5], A[6], A[7], A[0], A[1], A[2], A[3], A[4], R(35), K[35]);
    P(A[4], A[5], A[6], A[7], A[0], A[1], A[2], A[3], R(36), K[36]);
    P(A[3], A[4], A[5], A[6], A[7], A[0], A[1], A[2], R(37), K[37]);
    P(A[2], A[3], A[4], A[5], A[6], A[7], A[0], A[1], R(38), K[38]);
    P(A[1], A[2], A[3], A[4], A[5], A[6], A[7], A[0], R(39), K[39]);
    P(A[0], A[1], A[2], A[3], A[4], A[5], A[6], A[7], R(40), K[40]);
    P(A[7], A[0], A[1], A[2], A[3], A[4], A[5], A[6], R(41), K[41]);
    P(A[6], A[7], A[0], A[1], A[2], A[3], A[4], A[5], R(42), K[42]);
    P(A[5], A[6], A[7], A[0], A[1], A[2], A[3], A[4], R(43), K[43]);
    P(A[4], A[5], A[6], A[7], A[0], A[1], A[2], A[3], R(44), K[44]);
    P(A[3], A[4], A[5], A[6], A[7], A[0], A[1], A[2], R(45), K[45]);
    P(A[2], A[3], A[4], A[5], A[6], A[7], A[0], A[1], R(46), K[46]);
    P(A[1], A[2], A[3], A[4], A[5], A[6], A[7], A[0], R(47), K[47]);
    P(A[0], A[1], A[2], A[3], A[4], A[5], A[6], A[7], R(48), K[48]);
    P(A[7], A[0], A[1], A[2], A[3], A[4], A[5], A[6], R(49), K[49]);
    P(A[6], A[7], A[0], A[1], A[2], A[3], A[4], A[5], R(50), K[50]);
    P(A[5], A[6], A[7], A[0], A[1], A[2], A[3], A[4], R(51), K[51]);
    P(A[4], A[5], A[6], A[7], A[0], A[1], A[2], A[3], R(52), K[52]);
    P(A[3], A[4], A[5], A[6], A[7], A[0], A[1], A[2], R(53), K[53]);
    P(A[2], A[3], A[4], A[5], A[6], A[7], A[0], A[1], R(54), K[54]);
    P(A[1], A[2], A[3], A[4], A[5], A[6], A[7], A[0], R(55), K[55]);
    P(A[0], A[1], A[2], A[3], A[4], A[5], A[6], A[7], R(56), K[56]);
    P(A[7], A[0], A[1], A[2], A[3], A[4], A[5], A[6], R(57), K[57]);
    P(A[6], A[7], A[0], A[1], A[2], A[3], A[4], A[5], R(58), K[58]);
    P(A[5], A[6], A[7], A[0], A[1], A[2], A[3], A[4], R(59), K[59]);
    P(A[4], A[5], A[6], A[7], A[0], A[1], A[2], A[3], R(60), K[60]);

    // At this stage we can already figure out how many zeros we have at the end of the hash
    // and we can check if the hash is a valid block hash. This is called early exit optimisation.
    PUT_UINT32_BE(0x5BE0CD19 + A[7], doubleHash, 28);
    if (doubleHash[31] != 0 || doubleHash[30] != 0)
    {
        return 0;
    }

    P(A[3], A[4], A[5], A[6], A[7], A[0], A[1], A[2], R(61), K[61]);
    P(A[2], A[3], A[4], A[5], A[6], A[7], A[0], A[1], R(62), K[62]);
    P(A[1], A[2], A[3], A[4], A[5], A[6], A[7], A[0], R(63), K[63]);

    PUT_UINT32_BE(0x6A09E667 + A[0], doubleHash, 0);
    PUT_UINT32_BE(0xBB67AE85 + A[1], doubleHash, 4);
    PUT_UINT32_BE(0x3C6EF372 + A[2], doubleHash, 8);
    PUT_UINT32_BE(0xA54FF53A + A[3], doubleHash, 12);
    PUT_UINT32_BE(0x510E527F + A[4], doubleHash, 16);
    PUT_UINT32_BE(0x9B05688C + A[5], doubleHash, 20);
    PUT_UINT32_BE(0x1F83D9AB + A[6], doubleHash, 24);

    return 1;
}