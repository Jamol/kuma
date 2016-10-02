
; (function () {
    var kuma = {},
        uidSeed = 0;
    kuma.connIdSeed = 0;
    kuma.allocUniqueId = function () {
        return ++uidSeed;
    }

    window.kuma = kuma;
})();
